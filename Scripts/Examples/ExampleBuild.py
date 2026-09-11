"""Shared configure/build support for the example launchers."""

import os
from pathlib import Path
import platform
import shutil
import subprocess


SOURCE_DIRECTORY = Path(__file__).resolve().parents[2]
CPP_COMPONENTS = (
    'Install Visual Studio or Build Tools with "Desktop development with C++", '
    'including MSVC x64 tools and a Windows SDK.'
)


def read_cache(build_directory):
    cache_file = build_directory / "CMakeCache.txt"
    cache = {}
    if cache_file.is_file():
        for line in cache_file.read_text(encoding="utf-8", errors="replace").splitlines():
            if not line or line.startswith(("#", "//")):
                continue
            key, separator, value = line.partition("=")
            if separator:
                cache[key.split(":", 1)[0]] = value
    return cache


def find_visual_studio(env, cache):
    # A configured tree must keep its compiler installation/toolset.
    instance = cache.get("CMAKE_GENERATOR_INSTANCE", "").split(",", 1)[0]
    compiler = Path(cache.get("CMAKE_CXX_COMPILER", ""))
    toolset = None
    for parent in compiler.parents:
        if parent.parent.name.lower() == "msvc":
            toolset = parent.name
            instance = instance or str(parent.parents[3])
            break
    instance = instance or env.get("VSINSTALLDIR")
    if not instance:
        vswhere = shutil.which("vswhere.exe", path=env.get("PATH", ""))
        if not vswhere:
            candidate = Path(env.get("PROGRAMFILES(X86)", r"C:\Program Files (x86)")) / (
                "Microsoft Visual Studio/Installer/vswhere.exe")
            if candidate.is_file():
                vswhere = str(candidate)
        if vswhere:
            result = subprocess.run([
                vswhere, "-latest", "-products", "*", "-requires",
                "Microsoft.VisualStudio.Component.VC.Tools.x86.x64",
                "-property", "installationPath", "-utf8",
            ], env=env, capture_output=True, check=True)
            instance = result.stdout.decode("utf-8-sig").strip()
    return (Path(instance) if instance else None), toolset


def developer_environment(env, instance, toolset):
    script = instance / "Common7/Tools/VsDevCmd.bat"
    if not script.is_file():
        raise SystemExit(f"Visual Studio developer setup is missing: {script}. {CPP_COMPONENTS}")
    setup_env = dict(env, ARDA_VSDEVCMD=str(script), VSCMD_DEBUG="0")
    # Only a numeric cached MSVC version may enter the fixed cmd command.
    version = ""
    if toolset:
        if not all(part.isdecimal() for part in toolset.split(".")):
            raise SystemExit(f"Invalid cached MSVC toolset: {toolset}. Use a fresh build directory.")
        version = f" -vcvars_ver={toolset}"
    command = (
        '""%ARDA_VSDEVCMD%" -no_logo -arch=x64 -host_arch=x64'
        + version + ' >nul && set"'
    )
    # cmd uses different quoting from the C runtime's argv parser. Pass this
    # fixed command line directly; the script path is quoted via the child env.
    result = subprocess.run(
        "cmd.exe /d /u /v:off /s /c " + command,
        executable=env.get("COMSPEC", r"C:\Windows\System32\cmd.exe"),
        env=setup_env, capture_output=True, check=False,
    )
    if result.returncode:
        # stdout may contain environment values; never include it in diagnostics.
        raise SystemExit(f"Visual Studio developer setup failed ({result.returncode}). {CPP_COMPONENTS}")
    configured = {}
    for line in result.stdout.decode("utf-16-le").splitlines():
        key, separator, value = line.partition("=")
        if separator and key:
            configured[key.upper()] = value
    configured.pop("ARDA_VSDEVCMD", None)
    return configured


def prepare_build_environment(cache, generator):
    env = dict(os.environ)
    if platform.system() != "Windows":
        return shutil.which("cmake") or "cmake", env

    # Windows variable names are case insensitive, including Path/PATH.
    env = {key.upper(): value for key, value in env.items()}
    instance, toolset = find_visual_studio(env, cache)
    cached_compiler = cache.get("CMAKE_CXX_COMPILER", "")
    active_compiler = shutil.which("cl.exe", path=env.get("PATH", ""))
    ready = (
        active_compiler and env.get("INCLUDE") and env.get("LIB")
        and env.get("VSCMD_ARG_TGT_ARCH") == "x64"
        and (not cached_compiler or Path(active_compiler) == Path(cached_compiler))
        and all(shutil.which(tool, path=env.get("PATH", "")) for tool in ("link.exe", "rc.exe", "mt.exe"))
    )
    if not ready:
        if not instance:
            raise SystemExit(f"Cannot locate Visual Studio C++ build tools. {CPP_COMPONENTS}")
        print(f"Preparing Visual Studio x64 build tools from {instance}...", flush=True)
        env = developer_environment(env, instance, toolset)
    if not (env.get("INCLUDE") and env.get("LIB") and all(
        shutil.which(tool, path=env.get("PATH", "")) for tool in ("cl.exe", "link.exe", "rc.exe", "mt.exe")
    )):
        raise SystemExit(f"MSVC or Windows SDK tools are unavailable after setup. {CPP_COMPONENTS}")

    cmake_tools = instance / "Common7/IDE/CommonExtensions/Microsoft/CMake" if instance else None
    for name, cached, bundled in (
        ("cmake.exe", cache.get("CMAKE_COMMAND"), "CMake/bin/cmake.exe"),
        ("ninja.exe", cache.get("CMAKE_MAKE_PROGRAM") if "Ninja" in generator else None, "Ninja/ninja.exe"),
    ):
        if name == "ninja.exe" and "Ninja" not in generator:
            continue
        if shutil.which(name, path=env.get("PATH", "")):
            continue
        candidates = [Path(cached)] if cached else []
        if cmake_tools:
            candidates.append(cmake_tools / bundled)
        executable = next((path for path in candidates if path.is_file()), None)
        if executable is None:
            raise SystemExit(
                f'{name} is missing. Install "C++ CMake tools for Windows" in Visual Studio Installer, '
                'or install CMake 3.24+ and Ninja on PATH.'
            )
        env["PATH"] = str(executable.parent) + os.pathsep + env.get("PATH", "")
    return shutil.which("cmake.exe", path=env["PATH"]), env


def build_example(target, build_directory, configuration, options, generator=None):
    cache = read_cache(build_directory)
    new_build = not (build_directory / "CMakeCache.txt").is_file()
    if not generator and new_build and not os.environ.get("CMAKE_GENERATOR") and platform.system() == "Windows":
        generator = "Ninja"
    configure = ["-S", str(SOURCE_DIRECTORY), "-B", str(build_directory),
                 f"-DCMAKE_BUILD_TYPE={configuration}"]
    if generator:
        configure.extend(("-G", generator))
    configure.extend(options)
    selected_generator = cache.get("CMAKE_GENERATOR") or os.environ.get("CMAKE_GENERATOR") or ""
    for index, argument in enumerate(configure):
        if argument == "-G":
            selected_generator = configure[index + 1]
        elif argument.startswith("-G"):
            selected_generator = argument[2:]
    cmake, env = prepare_build_environment(cache, selected_generator)
    print(f'Configuring {target} in "{build_directory}"...', flush=True)
    subprocess.run([cmake, *configure], env=env, check=True)
    print(f"Building {target} ({configuration})...", flush=True)
    subprocess.run([
        cmake, "--build", str(build_directory), "--config", configuration,
        "--target", target, "--parallel",
    ], env=env, check=True)
