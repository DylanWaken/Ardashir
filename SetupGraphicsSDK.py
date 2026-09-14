#!/usr/bin/env python3
"""Provision Ardashir's graphics SDK components; Python 3.10+, no pip.

Downloads only missing compatible components. D3D12 uses the pinned Agility
NuGet package (headers, runtime and matching debug layer). Vulkan uses headers
and Khronos validation, including an existing LunarG SDK when available.
Vulkan validation source builds reuse Cmake/ProvisionValidation.cmake.
Windows setup installs/registers validation machine-wide (Administrator required).
Use --local-only for project-local setup, or --check for read-only discovery.
This does not install GPU drivers, Visual Studio, or the base Windows SDK.
"""
from __future__ import annotations

import argparse
import ctypes
import hashlib
import json
import os
from pathlib import Path
import platform
import re
import shutil
import subprocess
import sys
import tempfile
import urllib.request
import zipfile
import xml.etree.ElementTree as ET

REPO = Path(__file__).resolve().parent
VALIDATION_MANIFEST = "VkLayer_khronos_validation.json"
VALIDATION_REGISTRY_KEY = r"SOFTWARE\Khronos\Vulkan\ExplicitLayers"


def cmake_value(value: str | Path) -> str:
    value = str(value).replace("\\", "/")
    for size in range(1, 10):
        equals = "=" * size
        if f"]{equals}]" not in value:
            return f"[{equals}[{value}]{equals}]"
    raise ValueError("Cannot encode CMake path")


def pins() -> tuple[str, str, str]:
    """Read the build's authoritative pins rather than maintain another version list."""
    text = (REPO / "Cmake/Dependencies.cmake").read_text(encoding="utf-8")
    def field(dependency: str, name: str) -> str:
        block = re.search(r"FetchContent_Declare\(\s*" + dependency + r"\b(.*?)\)", text, re.S)
        match = re.search(r"\b" + name + r'\s+"([^"]+)"', block[1] if block else "")
        if not match:
            raise RuntimeError(f"Cannot read {dependency} {name} from Dependencies.cmake")
        return match[1]
    return (field("ardashir_d3d12_agility", "URL"),
            field("ardashir_d3d12_agility", "URL_HASH").removeprefix("SHA256="),
            field("ardashir_vulkan_headers", "GIT_TAG"))


def run(arguments: list[str | Path]) -> None:
    print("Running:", subprocess.list2cmdline(list(map(str, arguments))), flush=True)
    subprocess.run(list(map(str, arguments)), check=True)


def download(url: str, destination: Path, expected_hash: str) -> None:
    def valid(path: Path) -> bool:
        if not path.is_file():
            return False
        with path.open("rb") as stream:
            digest = hashlib.sha256()
            for block in iter(lambda: stream.read(1024 * 1024), b""):
                digest.update(block)
        return digest.hexdigest().lower() == expected_hash.lower()
    if valid(destination):
        print(f"Reusing verified archive: {destination}", flush=True)
        return
    destination.parent.mkdir(parents=True, exist_ok=True)
    partial = destination.with_suffix(destination.suffix + ".partial")
    print(f"Downloading {url}", flush=True)
    try:
        request = urllib.request.Request(url, headers={"User-Agent": "Ardashir-Graphics-Setup"})
        with urllib.request.urlopen(request, timeout=60) as response, partial.open("wb") as output:
            shutil.copyfileobj(response, output, 1024 * 1024)
        if not valid(partial):
            raise RuntimeError(f"SHA-256 mismatch for {url}; archive rejected")
        partial.replace(destination)
    finally:
        partial.unlink(missing_ok=True)


def extract_zip(archive: Path, destination: Path) -> None:
    destination.mkdir(parents=True, exist_ok=True)
    with zipfile.ZipFile(archive) as source:
        for item in source.infolist():
            target = (destination / item.filename.replace("\\", "/")).resolve()
            if not target.is_relative_to(destination.resolve()) or ":" in item.filename:
                raise RuntimeError(f"Archive member escapes installation: {item.filename}")
            if (item.external_attr >> 16) & 0o170000 == 0o120000:
                raise RuntimeError(f"Archive symlink is unsupported: {item.filename}")
        source.extractall(destination)


def cache_values(path: Path) -> dict[str, str]:
    if not path.is_file():
        return {}
    return dict(re.findall(r"^([^#/:\n][^:\n]*):[^=\n]+=(.*)$", path.read_text(encoding="utf-8"), re.M))


def sdk_roots() -> list[Path]:
    roots = [Path(os.environ["VULKAN_SDK"])] if os.environ.get("VULKAN_SDK") else []
    if os.name == "nt":
        roots.extend(sorted(Path("C:/VulkanSDK").glob("*"), reverse=True))
    return roots


def load_library(path: Path) -> None:
    if not path.is_file():
        raise RuntimeError(f"Missing library: {path}")
    if os.name == "nt":
        # A damaged DLL must return an error, not display a blocking Bad Image dialog.
        kernel = ctypes.WinDLL("kernel32", use_last_error=True)
        previous = ctypes.c_uint()
        kernel.SetThreadErrorMode(0x8003, ctypes.byref(previous))
        try:
            with os.add_dll_directory(str(path.parent.resolve())):
                ctypes.WinDLL(str(path.resolve()))
        finally:
            kernel.SetThreadErrorMode(previous.value, None)
    else:
        ctypes.CDLL(str(path.resolve()))


def layer_library(directory: Path) -> Path:
    manifest = directory / "VkLayer_khronos_validation.json"
    layer = json.loads(manifest.read_text(encoding="utf-8"))["layer"]
    if layer["name"] != "VK_LAYER_KHRONOS_validation":
        raise ValueError("Manifest is not Khronos validation")
    library = Path(layer["library_path"])
    if not library.is_absolute():
        library = directory / library
    # Installed Linux manifests sometimes name the library without a directory.
    if not library.is_file() and os.name != "nt" and len(Path(layer["library_path"]).parts) == 1:
        for root in (directory, directory.parent.parent.parent / "lib", Path("/usr/lib"),
                     Path("/usr/lib/x86_64-linux-gnu"), Path("/usr/local/lib")):
            candidate = root / layer["library_path"]
            if candidate.is_file():
                library = candidate
                break
    load_library(library)
    return library.resolve()


def registered_layers(system_only: bool = False) -> list[Path]:
    if os.name != "nt":
        return []
    import winreg
    result = []
    hives = (winreg.HKEY_LOCAL_MACHINE,) if system_only else (winreg.HKEY_CURRENT_USER, winreg.HKEY_LOCAL_MACHINE)
    for hive in hives:
        try:
            with winreg.OpenKey(hive, VALIDATION_REGISTRY_KEY, 0, winreg.KEY_READ | winreg.KEY_WOW64_64KEY) as key:
                for index in range(winreg.QueryInfoKey(key)[1]):
                    name, enabled, kind = winreg.EnumValue(key, index)
                    if enabled == 0 and kind == winreg.REG_DWORD and Path(name).name == VALIDATION_MANIFEST:
                        result.append(Path(name).parent)
        except OSError:
            pass
    return result


def windows_administrator() -> bool:
    return os.name == "nt" and bool(ctypes.windll.shell32.IsUserAnAdmin())


def system_validation_root() -> Path:
    import winreg
    with winreg.OpenKey(winreg.HKEY_LOCAL_MACHINE, r"SOFTWARE\Microsoft\Windows\CurrentVersion",
                        0, winreg.KEY_READ | winreg.KEY_WOW64_64KEY) as key:
        program_files = Path(winreg.QueryValueEx(key, "ProgramFilesDir")[0])
    return program_files / "Ardashir/VulkanValidation"


def reject_redirected_path(path: Path) -> None:
    # An administrator must not install through a pre-existing junction/symlink.
    for part in (path, *path.parents):
        try:
            if getattr(part.lstat(), "st_file_attributes", 0) & 0x400 or part.is_symlink():
                raise RuntimeError(f"System validation destination contains a reparse point: {part}")
        except FileNotFoundError:
            pass


def protect_install_directory(directory: Path) -> None:
    reject_redirected_path(directory)
    directory.mkdir(parents=True, exist_ok=True)
    # Reset previous explicit grants, then retain only administrator/system write
    # access and ordinary-user read/execute access. SID syntax is locale independent.
    for arguments in (["/reset"], ["/inheritance:r", "/grant:r", "*S-1-5-18:(OI)(CI)F",
                                  "*S-1-5-32-544:(OI)(CI)F", "*S-1-5-32-545:(OI)(CI)RX"]):
        subprocess.run(["icacls.exe", str(directory), *arguments, "/q"], check=True, capture_output=True)


def write_installed_file(path: Path, data: bytes) -> None:
    reject_redirected_path(path)
    if path.is_file() and path.read_bytes() == data:
        return
    # Publish a complete file atomically, retaining inherited Program Files ACLs.
    with tempfile.NamedTemporaryFile(dir=path.parent, delete=False) as stream:
        temporary = Path(stream.name)
        stream.write(data)
    try:
        temporary.replace(path)
    finally:
        temporary.unlink(missing_ok=True)


def probe_vulkan_validation() -> None:
    """Called in a fresh process: vkCreateInstance must actually load validation."""
    class InstanceCreateInfo(ctypes.Structure):
        _fields_ = [("sType", ctypes.c_uint32), ("pNext", ctypes.c_void_p), ("flags", ctypes.c_uint32),
                    ("pApplicationInfo", ctypes.c_void_p), ("enabledLayerCount", ctypes.c_uint32),
                    ("ppEnabledLayerNames", ctypes.POINTER(ctypes.c_char_p)),
                    ("enabledExtensionCount", ctypes.c_uint32),
                    ("ppEnabledExtensionNames", ctypes.POINTER(ctypes.c_char_p))]
    loader = ctypes.WinDLL("vulkan-1.dll")
    create = loader.vkCreateInstance
    create.argtypes = [ctypes.POINTER(InstanceCreateInfo), ctypes.c_void_p, ctypes.POINTER(ctypes.c_void_p)]
    create.restype = ctypes.c_int32
    destroy = loader.vkDestroyInstance
    destroy.argtypes = [ctypes.c_void_p, ctypes.c_void_p]
    destroy.restype = None
    names = (ctypes.c_char_p * 1)(b"VK_LAYER_KHRONOS_validation")
    info = InstanceCreateInfo(sType=1, enabledLayerCount=1, ppEnabledLayerNames=names)
    instance = ctypes.c_void_p()
    result = create(ctypes.byref(info), None, ctypes.byref(instance))
    if result != 0:
        raise RuntimeError(f"Vulkan loader could not create an instance with Khronos validation (VkResult {result}).")
    destroy(instance, None)


def verify_system_validation() -> None:
    environment = dict(os.environ)
    # Verify registry discovery independently of this shell's local SDK paths.
    for name in ("VK_LAYER_PATH", "VK_ADD_LAYER_PATH"):
        environment.pop(name, None)
    result = subprocess.run([sys.executable, str(Path(__file__).resolve()), "--_probe-vulkan"],
                            env=environment, capture_output=True, text=True, timeout=30)
    if result.returncode:
        raise RuntimeError("System Vulkan validation verification failed:\n" + (result.stderr or result.stdout).strip())


def register_system_validation(manifest: Path, root: Path) -> None:
    import winreg
    with winreg.CreateKeyEx(winreg.HKEY_LOCAL_MACHINE, VALIDATION_REGISTRY_KEY, 0,
                           winreg.KEY_READ | winreg.KEY_WRITE | winreg.KEY_WOW64_64KEY) as key:
        previous = {}
        for index in range(winreg.QueryInfoKey(key)[1]):
            name, value, kind = winreg.EnumValue(key, index)
            path = Path(name)
            if path.name == VALIDATION_MANIFEST and path.is_absolute() and path.is_relative_to(root):
                previous[name] = (value, kind)
        try:
            winreg.SetValueEx(key, str(manifest), 0, winreg.REG_DWORD, 0)
            for name in previous:
                if name != str(manifest):
                    winreg.DeleteValue(key, name)
            verify_system_validation()
        except (OSError, RuntimeError, subprocess.SubprocessError):
            # Roll back only registrations owned by this installer. Other SDKs
            # remain untouched, including disabled entries and their value types.
            if str(manifest) not in previous:
                try:
                    winreg.DeleteValue(key, str(manifest))
                except FileNotFoundError:
                    pass
            for name, (value, kind) in previous.items():
                winreg.SetValueEx(key, name, 0, kind, value)
            raise


def install_system_validation(source: Path) -> Path:
    if not windows_administrator():
        raise RuntimeError("Windows system-wide validation setup requires Administrator privileges. "
                           "Run this script in an Administrator terminal, or use --local-only.")
    library = layer_library(source)
    manifest = json.loads((source / VALIDATION_MANIFEST).read_text(encoding="utf-8"))
    manifest["layer"]["library_path"] = ".\\" + library.name
    # Include adjacent runtime dependencies (but not SDK executables or PDBs).
    files = {path.name: path.read_bytes() for path in sorted(library.parent.glob("*.dll"))}
    files[library.name] = library.read_bytes()
    files[VALIDATION_MANIFEST] = (json.dumps(manifest, indent=2, ensure_ascii=False) + "\n").encode("utf-8")
    digest = hashlib.sha256()
    for name, content in sorted(files.items()):
        digest.update(name.encode("utf-8") + b"\0" + hashlib.sha256(content).digest())
    root = system_validation_root()
    protect_install_directory(root)
    # Content-addressed versions avoid overwriting a DLL in use by another app.
    destination = root / digest.hexdigest()
    protect_install_directory(destination)
    for name, content in files.items():
        write_installed_file(destination / name, content)
    layer_library(destination)
    register_system_validation(destination / VALIDATION_MANIFEST, root)
    print(f"System-wide Vulkan validation registered and loader-verified: {destination}", flush=True)
    return destination


def find_layer(candidates: list[Path]) -> Path | None:
    for path in dict.fromkeys(candidates):
        if not (path / "VkLayer_khronos_validation.json").is_file():
            continue
        try:
            layer_library(path)
            return path.resolve()
        except (OSError, ValueError, KeyError, RuntimeError) as error:
            print(f"Unusable validation at {path}: {error}", flush=True)
    return None


def compatible_headers(path: Path, tag: str) -> bool:
    core = path / "vulkan/vulkan_core.h"
    if not core.is_file() or not (path / "vulkan/vulkan.hpp").is_file():
        return False
    source = core.read_text(encoding="utf-8")
    patch = re.search(r"#define\s+VK_HEADER_VERSION\s+(\d+)", source)
    major_minor = re.search(r"VK_MAKE_API_VERSION\(\s*\d+,\s*(\d+),\s*(\d+),\s*VK_HEADER_VERSION", source)
    return bool(patch and major_minor and tuple(map(int, (*major_minor.groups(), patch[1]))) >=
                tuple(map(int, tag.removeprefix("v").split("."))))


def compatible_agility(path: Path, version: str) -> bool:
    required = ("build/native/include/d3d12.h", "build/native/bin/x64/D3D12Core.dll",
                "build/native/bin/x64/d3d12SDKLayers.dll")
    if not all((path / name).is_file() for name in required):
        return False
    try:
        manifest = ET.parse(path / "Microsoft.Direct3D.D3D12.nuspec")
        if not any(node.tag.rsplit("}", 1)[-1] == "version" and node.text == version for node in manifest.iter()):
            return False
        for name in required[1:]:
            load_library(path / name)
        return True
    except (OSError, RuntimeError, ET.ParseError) as error:
        print(f"Unusable Agility package at {path}: {error}", flush=True)
        return False


def generator(explicit: str | None) -> tuple[str, str]:
    if explicit:
        return explicit, "x64" if explicit.startswith("Visual Studio") else ""
    if os.name != "nt" or shutil.which("cl"):
        return ("Ninja" if shutil.which("ninja") else "NMake Makefiles" if os.name == "nt" else "Unix Makefiles"), ""
    vswhere = Path(os.environ.get("ProgramFiles(x86)", "C:/Program Files (x86)")) / "Microsoft Visual Studio/Installer/vswhere.exe"
    if vswhere.is_file():
        result = subprocess.run([str(vswhere), "-latest", "-products", "*", "-requires",
                                 "Microsoft.VisualStudio.Component.VC.Tools.x86.x64", "-format", "json"],
                                check=True, capture_output=True, text=True)
        installs = json.loads(result.stdout)
        if installs:
            major = installs[0]["installationVersion"].split(".")[0]
            capabilities = subprocess.run(["cmake", "-E", "capabilities"], check=True, capture_output=True, text=True)
            for supported in json.loads(capabilities.stdout)["generators"]:
                if supported["name"].startswith(f"Visual Studio {major} "):
                    return supported["name"], "x64"
    raise RuntimeError("Building missing Vulkan validation needs a C++ compiler and the base Windows SDK. "
                       "Use a Visual Studio C++ developer shell or an installed LunarG SDK.")


def provision_layer(root: Path, selected_generator: str | None) -> Path:
    if not shutil.which("cmake") or not shutil.which("git"):
        raise RuntimeError("Building missing Vulkan validation requires CMake 3.24+ and Git on PATH.")
    name, architecture = generator(selected_generator)
    validation = root / "validation-layers"
    validation.mkdir(parents=True, exist_ok=True)
    # Avoid the optional provisioner's manifest-only discovery: candidates above
    # were load-tested. Its source/build/install pipeline remains authoritative.
    request = validation / "setup-request.cmake"
    values = {"VALIDATION_ROOT": validation, "PROVISION": "ON", "VULKAN_ENABLED": "ON",
              "LAYER_DIR": "", "LAYER_SOURCE": "", "GENERATOR": name,
              "GENERATOR_PLATFORM": architecture, "GENERATOR_TOOLSET": "", "TOOLCHAIN": "",
              "MAKE_PROGRAM": "", "CROSS_COMPILING": "OFF", "FORCE_PROVISION": "ON",
              "SETUP_PYTHON_EXECUTABLE": Path(sys.executable)}
    request.write_text("\n".join(f"set({key} {cmake_value(value)})" for key, value in values.items()) + "\n", encoding="utf-8")
    run(["cmake", f"-DREQUEST={request.as_posix()}", "-P", REPO / "Cmake/ProvisionValidation.cmake"])
    layer = find_layer([validation / "install/bin", validation / "install/share/vulkan/explicit_layer.d"])
    if layer is None:
        raise RuntimeError(f"Vulkan validation did not produce a loadable layer. Read logs in {validation}")
    return layer


def write_configuration(root: Path, agility: Path | None, headers: Path | None, layer: Path | None) -> None:
    variables = {}
    if agility:
        variables["FETCHCONTENT_SOURCE_DIR_ARDASHIR_D3D12_AGILITY"] = agility
    if headers:
        variables["ARDASHIR_VULKAN_INCLUDE_DIR"] = headers
    if layer:
        variables["ARDASHIR_VULKAN_VALIDATION_DIR"] = layer
    config = root / "GraphicsSdk.cmake"
    config.write_text("# Generated by SetupGraphicsSDK.py; opt in with cmake -C.\n" +
                      "\n".join(f'set({key} {cmake_value(value)} CACHE PATH "Graphics SDK setup" FORCE)'
                                for key, value in variables.items()) + "\n", encoding="utf-8")
    # Launchers fill only unspecified SDK settings; explicit -C above still
    # lets a user deliberately replace the SDK paths in an existing build.
    defaults = root / "GraphicsSdkDefaults.cmake"
    defaults.write_text("# Generated SDK defaults for the example launchers.\n" +
                        "\n".join(f'if(NOT DEFINED {key} OR "${{{key}}}" STREQUAL "")\n'
                                  f'  set({key} {cmake_value(value)} CACHE PATH "Graphics SDK setup" FORCE)\n'
                                  'endif()' for key, value in variables.items()) + "\n", encoding="utf-8")
    powershell = "# Dot-source this file in the shell that runs graphics applications.\n"
    shell = "# Source this file in the shell that runs graphics applications.\n"
    if layer and layer.resolve() not in {path.resolve() for path in registered_layers()}:
        ps_path = str(layer).replace("'", "''")
        sh_path = str(layer).replace("'", "'\"'\"'")
        powershell += (
            "if (-not (Test-Path Env:VK_LAYER_PATH)) {\n"
            f"  if (($env:VK_ADD_LAYER_PATH -split [regex]::Escape([IO.Path]::PathSeparator)) -notcontains '{ps_path}') {{\n"
            f"    $env:VK_ADD_LAYER_PATH = '{ps_path}' + $(if ($env:VK_ADD_LAYER_PATH) {{ [IO.Path]::PathSeparator + $env:VK_ADD_LAYER_PATH }} else {{ '' }})\n"
            "  }\n}\n")
        shell += (
            'if [ "${VK_LAYER_PATH+x}" != x ]; then\n'
            '  case ":${VK_ADD_LAYER_PATH-}:" in\n'
            f"    *:'{sh_path}':*) ;;\n"
            f"    *) export VK_ADD_LAYER_PATH='{sh_path}'${{VK_ADD_LAYER_PATH:+:$VK_ADD_LAYER_PATH}} ;;\n"
            "  esac\nfi\n")
    elif layer:
        # Adding a registered manifest through the environment discovers it a
        # second time and makes the loader emit duplicate-layer warnings.
        powershell += "# This validation layer is registered with the system loader; no path override is needed.\n"
        shell += "# This validation layer is registered with the system loader; no path override is needed.\n"
    (root / "ActivateGraphicsSDK.ps1").write_text(powershell, encoding="utf-8")
    (root / "activate-graphics-sdk.sh").write_text(shell, encoding="utf-8")
    print(f'\nUse this setup in a project build:\n  cmake -S "{REPO}" -B build/dev -C "{config}"')
    activation = root / ("ActivateGraphicsSDK.ps1" if os.name == "nt" else "activate-graphics-sdk.sh")
    print(f'Vulkan runtime shell setup:\n  . "{activation}"')
    if root == (REPO / "build/graphics-sdk").resolve():
        print("Scripts/Examples launchers automatically use this setup for unspecified SDK paths.")


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--root", type=Path, default=REPO / "build/graphics-sdk", help="local download/build directory")
    parser.add_argument("--build-dir", action="append", type=Path, default=[], help="also inspect this existing CMake build (repeatable)")
    parser.add_argument("--backend", choices=("all", "d3d12", "vulkan"), default="all")
    parser.add_argument("--generator", help="CMake generator for a missing validation-layer source build")
    parser.add_argument("--check", action="store_true", help="read-only discovery; no downloads, builds or generated files")
    parser.add_argument("--local-only", action="store_true", help="skip Windows system-wide validation installation/registration")
    options = parser.parse_args(argv)
    if platform.system() not in ("Windows", "Linux") or platform.machine().lower() not in ("amd64", "x86_64") or sys.maxsize <= 2**32:
        parser.error("This project's native SDK setup supports 64-bit Python on Windows/Linux x64.")
    if options.backend == "d3d12" and os.name != "nt":
        parser.error("D3D12 SDK setup requires Windows.")
    root = options.root.resolve()
    url, digest, tag = pins()
    version = url.rsplit("/", 1)[-1]
    caches = [p / "CMakeCache.txt" for p in options.build_dir]
    caches += [REPO / "build/CMakeCache.txt", *sorted((REPO / "build").glob("*/CMakeCache.txt"))]
    values = [cache_values(p) for p in dict.fromkeys(caches)]
    dep_roots = [REPO / "build/_deps"] + [Path(v["FETCHCONTENT_BASE_DIR"]) for v in values if v.get("FETCHCONTENT_BASE_DIR")]
    agility = headers = layer = None
    missing = []
    if options.backend != "vulkan" and os.name == "nt":
        candidates = [root / "agility", Path.home() / ".nuget/packages/microsoft.direct3d.d3d12" / version]
        candidates += [p / "ardashir_d3d12_agility-src" for p in dep_roots]
        candidates += [Path(v["FETCHCONTENT_SOURCE_DIR_ARDASHIR_D3D12_AGILITY"]) for v in values if v.get("FETCHCONTENT_SOURCE_DIR_ARDASHIR_D3D12_AGILITY")]
        agility = next((p.resolve() for p in candidates if compatible_agility(p, version)), None)
        if not agility and not options.check:
            archive = root / f"downloads/agility-{version}.zip"
            download(url, archive, digest)
            extract_zip(archive, root / "agility")
            agility = root / "agility" if compatible_agility(root / "agility", version) else None
        print(f"D3D12 Agility {version} + matching debug layer: {agility or 'MISSING'}", flush=True)
        if not agility:
            missing.append("D3D12 Agility/debug layer")
    if options.backend != "d3d12":
        installed_sdks = sdk_roots()
        candidates = [p / "Include" for p in installed_sdks] + [p / "include" for p in installed_sdks]
        candidates += [root / "vulkan-headers/include", *[p / "ardashir_vulkan_headers-src/include" for p in dep_roots]]
        candidates += [Path(v["ARDASHIR_VULKAN_INCLUDE_DIR"]) for v in values if v.get("ARDASHIR_VULKAN_INCLUDE_DIR")]
        if os.name != "nt":
            candidates += [Path("/usr/include"), Path("/usr/local/include")]
        headers = next((p.resolve() for p in candidates if compatible_headers(p, tag)), None)
        if not headers and not options.check:
            if not shutil.which("git"):
                raise RuntimeError("Downloading Vulkan headers requires Git on PATH.")
            root.mkdir(parents=True, exist_ok=True)
            with tempfile.TemporaryDirectory(prefix="headers-", dir=root) as staging:
                source = Path(staging) / "source"
                run(["git", "clone", "--depth", "1", "--branch", tag, "https://github.com/KhronosGroup/Vulkan-Headers.git", source])
                if not compatible_headers(source / "include", tag):
                    raise RuntimeError("Downloaded Vulkan headers failed version validation")
                shutil.copytree(source, root / "vulkan-headers", dirs_exist_ok=True, ignore=shutil.ignore_patterns(".git"))
            headers = root / "vulkan-headers/include"
        print(f"Vulkan headers >= {tag}: {headers or 'MISSING'}", flush=True)
        if not headers:
            missing.append("Vulkan headers")
        if "VK_LAYER_PATH" in os.environ:
            candidates = [Path(p) for p in os.environ["VK_LAYER_PATH"].split(os.pathsep) if p]
            layer = find_layer(candidates)
            if not layer:
                raise RuntimeError("Explicit VK_LAYER_PATH has no loadable Khronos layer. Correct/unset it, then rerun setup.")
        else:
            candidates = [Path(p) for p in os.environ.get("VK_ADD_LAYER_PATH", "").split(os.pathsep) if p]
            candidates += registered_layers()
            for sdk in installed_sdks:
                candidates += [sdk / "Bin", sdk / "etc/vulkan/explicit_layer.d", sdk / "share/vulkan/explicit_layer.d"]
            candidates += [Path(v["ARDASHIR_VULKAN_VALIDATION_DIR"]) for v in values if v.get("ARDASHIR_VULKAN_VALIDATION_DIR")]
            for base in [root, *[p.parent for p in caches]]:
                candidates += [base / "validation-layers/install/bin", base / "validation-layers/install/share/vulkan/explicit_layer.d"]
                path_file = base / "validation-layers/layer-path.txt"
                if path_file.is_file() and path_file.read_text(encoding="utf-8").strip():
                    candidates.append(Path(path_file.read_text(encoding="utf-8").strip()))
            if os.name != "nt":
                candidates += [Path(p) for p in ("/etc/vulkan/explicit_layer.d", "/usr/share/vulkan/explicit_layer.d", "/usr/local/share/vulkan/explicit_layer.d")]
                candidates += [Path.home() / ".local/share/vulkan/explicit_layer.d"]
            layer = find_layer(candidates)
            if not layer and not options.check:
                layer = provision_layer(root, options.generator)
        print(f"Loadable Vulkan validation: {layer or 'MISSING'}", flush=True)
        if not layer:
            missing.append("Vulkan validation")
    if missing:
        print("Missing: " + ", ".join(missing), file=sys.stderr)
        return 1
    if layer and os.name == "nt" and not options.local_only:
        if options.check:
            if not find_layer(registered_layers(system_only=True)):
                print("Missing: system-wide Vulkan validation registration. Run SetupGraphicsSDK.py "
                      "in an Administrator terminal, or use --local-only to check only local components.", file=sys.stderr)
                return 1
            verify_system_validation()
            print("System-wide Vulkan validation: loader verification passed.")
        else:
            layer = install_system_validation(layer)
    if not options.check:
        root.mkdir(parents=True, exist_ok=True)
        write_configuration(root, agility, headers, layer)
    if layer and options.local_only and windows_administrator():
        print("Warning: this process is elevated. Vulkan ignores VK_LAYER_PATH and VK_ADD_LAYER_PATH "
              "in elevated applications. A loadable local DLL does not establish loader visibility; "
              "run examples from a non-elevated terminal, or use a system-installed validation layer.")
    print("SDK components ready. GPU drivers and native device/validation support are checked by the application.")
    return 0


if __name__ == "__main__":
    try:
        if sys.argv[1:] == ["--_probe-vulkan"]:
            probe_vulkan_validation()
        else:
            raise SystemExit(main())
    except (OSError, ValueError, RuntimeError, subprocess.SubprocessError) as error:
        print(f"Graphics SDK setup failed: {error}", file=sys.stderr)
        raise SystemExit(1)
