#!/usr/bin/env python3
"""Provision Ardashir's graphics SDK components locally; Python 3.10+, no pip.

Downloads only missing compatible components. D3D12 uses the pinned Agility
NuGet package (headers, runtime and matching debug layer). Vulkan uses headers
and Khronos validation, including an existing LunarG SDK when available.
Vulkan validation source builds reuse Cmake/ProvisionValidation.cmake.
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


def registered_layers() -> list[Path]:
    if os.name != "nt":
        return []
    import winreg
    result = []
    for hive in (winreg.HKEY_CURRENT_USER, winreg.HKEY_LOCAL_MACHINE):
        try:
            with winreg.OpenKey(hive, r"SOFTWARE\Khronos\Vulkan\ExplicitLayers") as key:
                for index in range(winreg.QueryInfoKey(key)[1]):
                    name, enabled, _ = winreg.EnumValue(key, index)
                    if enabled == 0 and Path(name).name == "VkLayer_khronos_validation.json":
                        result.append(Path(name).parent)
        except OSError:
            pass
    return result


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
    powershell = "# Dot-source this file in the shell that runs graphics applications.\n"
    shell = "# Source this file in the shell that runs graphics applications.\n"
    if layer:
        ps_path = str(layer).replace("'", "''")
        sh_path = str(layer).replace("'", "'\"'\"'")
        powershell += (f"if ($env:VK_LAYER_PATH) {{ Write-Warning 'VK_LAYER_PATH remains authoritative; clear it to use this layer.' }}\n"
                       f"$env:VK_ADD_LAYER_PATH = '{ps_path}' + $(if ($env:VK_ADD_LAYER_PATH) {{ [IO.Path]::PathSeparator + $env:VK_ADD_LAYER_PATH }} else {{ '' }})\n")
        shell += f"export VK_ADD_LAYER_PATH='{sh_path}'${{VK_ADD_LAYER_PATH:+:$VK_ADD_LAYER_PATH}}\n"
    (root / "ActivateGraphicsSDK.ps1").write_text(powershell, encoding="utf-8")
    (root / "activate-graphics-sdk.sh").write_text(shell, encoding="utf-8")
    print(f'\nUse this setup in a project build:\n  cmake -S "{REPO}" -B build/dev -C "{config}"')
    activation = root / ("ActivateGraphicsSDK.ps1" if os.name == "nt" else "activate-graphics-sdk.sh")
    print(f'Vulkan runtime shell setup:\n  . "{activation}"')


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--root", type=Path, default=REPO / "build/graphics-sdk", help="local download/build directory")
    parser.add_argument("--build-dir", action="append", type=Path, default=[], help="also inspect this existing CMake build (repeatable)")
    parser.add_argument("--backend", choices=("all", "d3d12", "vulkan"), default="all")
    parser.add_argument("--generator", help="CMake generator for a missing validation-layer source build")
    parser.add_argument("--check", action="store_true", help="read-only discovery; no downloads, builds or generated files")
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
    if not options.check:
        root.mkdir(parents=True, exist_ok=True)
        write_configuration(root, agility, headers, layer)
    print("SDK components ready. GPU drivers and native device/validation support are checked by the application.")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (OSError, ValueError, RuntimeError, subprocess.CalledProcessError) as error:
        print(f"Graphics SDK setup failed: {error}", file=sys.stderr)
        raise SystemExit(1)
