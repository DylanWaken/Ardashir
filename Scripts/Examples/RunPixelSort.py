#!/usr/bin/env python3
"""Build and launch the Windows Pixel Sort example, or run an existing build."""

import argparse
import math
import os
import platform
import subprocess
from pathlib import Path


SOURCE_DIRECTORY = Path(__file__).resolve().parents[2]


def parse_arguments(argv=None) -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Configure, build, and run Pixel Sort on D3D12 or Vulkan.",
        epilog="Build in a developer shell with a CUDA toolkit. --run-only needs neither CMake nor nvcc.",
    )
    parser.add_argument("backend", nargs="?", choices=("d3d12", "vulkan"), default="d3d12")
    parser.add_argument("build_directory", nargs="?", type=Path,
                        default=SOURCE_DIRECTORY / "build" / "pixel-sort")
    parser.add_argument("configuration", nargs="?", default="Release")
    parser.add_argument("--run-only", action="store_true", help="Skip configuration and compilation.")
    parser.add_argument("--generator", help="CMake generator; new builds default to Ninja unless CMAKE_GENERATOR is set.")
    parser.add_argument("--nvcc", type=Path, help="Build-time path to nvcc.exe (Ninja/Makefiles).")
    parser.add_argument("--cuda-include-dir", type=Path, help="CUDA SDK include directory for the backend.")
    parser.add_argument("--architectures", help='Native CUDA targets, e.g. "80;120"; otherwise use CMake defaults/cache.')
    parser.add_argument("--cmake-arg", action="append", default=[], metavar="ARG",
                        help="Extra configure argument; repeat as --cmake-arg=-DNAME=VALUE.")
    parser.add_argument("--cuda-mode", choices=("auto", "context", "graphics"), default="auto")
    for name in ("width", "height", "frames", "channel", "threshold"):
        parser.add_argument(f"--{name}", type=int)
    parser.add_argument("--time", type=float)
    parser.add_argument("--capture", type=Path)
    for name in ("hidden", "verify", "resize-test", "validation"):
        parser.add_argument(f"--{name}", action="store_true")
    args = parser.parse_args(argv)
    for name, maximum in (("width", 8192), ("height", 8192), ("frames", 0xFFFFFFFF),
                          ("channel", 2), ("threshold", 255)):
        value = getattr(args, name)
        minimum = 1 if name in ("width", "height") else 0
        if value is not None and not minimum <= value <= maximum:
            parser.error(f"--{name} must be in [{minimum}, {maximum}].")
    if args.time is not None and not math.isfinite(args.time):
        parser.error("--time must be finite.")
    return args


def main(argv=None) -> int:
    args = parse_arguments(argv)
    if platform.system() != "Windows":
        raise SystemExit("Pixel Sort requires Windows, including when using Vulkan.")
    build_directory = args.build_directory.resolve()
    try:
        if not args.run_only:
            configure = [
                "cmake", "-S", str(SOURCE_DIRECTORY), "-B", str(build_directory),
                "-DARDASHIR_ENABLE_CUDA=ON", "-DARDASHIR_BUILD_CUDA_KERNELS=ON",
                "-DARDASHIR_BUILD_PIXEL_SORT=ON",
                f"-DARDASHIR_BACKEND_{args.backend.upper()}=ON",
                f"-DCMAKE_BUILD_TYPE={args.configuration}",
            ]
            # Isolate a new example build without changing an existing tree's test settings.
            new_build = not (build_directory / "CMakeCache.txt").is_file()
            if new_build:
                configure.extend(f"-DARDASHIR_BUILD_{name}=OFF" for name in
                                 ("TESTS", "RHI_TEST", "ARDG_EXAMPLE", "CORNELL_BOX"))
            generator = args.generator
            if not generator and new_build and not os.environ.get("CMAKE_GENERATOR"):
                generator = "Ninja"
            if generator:
                configure.extend(("-G", generator))
            cuda_include = args.cuda_include_dir
            if args.nvcc:
                nvcc = args.nvcc.resolve()
                configure.append(f"-DCMAKE_CUDA_COMPILER={nvcc}")
                if cuda_include is None:
                    cuda_include = nvcc.parent.parent / "include"
            if cuda_include is not None:
                configure.append(f"-DARDASHIR_CUDA_INCLUDE_DIR={cuda_include.resolve()}")
            if args.architectures:
                configure.append(f"-DARDASHIR_CUDA_ARCHITECTURES={args.architectures}")
            configure.extend(args.cmake_arg)
            print(f'Configuring PixelSort in "{build_directory}"...', flush=True)
            subprocess.run(configure, check=True)
            print(f"Building PixelSort ({args.configuration})...", flush=True)
            subprocess.run([
                "cmake", "--build", str(build_directory), "--config", args.configuration,
                "--target", "PixelSort", "--parallel",
            ], check=True)

        output_directory = build_directory / "Source" / "ArdaTests" / "Examples" / "PixelSort"
        candidates = (output_directory / args.configuration / "PixelSort.exe",
                      output_directory / "PixelSort.exe")
        executable = next((path for path in candidates if path.is_file()), None)
        if executable is None:
            raise SystemExit(
                f"PixelSort.exe was not found under {output_directory}. "
                "Check the build directory/configuration, or omit --run-only to build it."
            )

        command = [str(executable), "--backend", args.backend, "--cuda-mode", args.cuda_mode]
        for name in ("width", "height", "frames", "channel", "threshold", "time", "capture"):
            value = getattr(args, name)
            if value is not None:
                command.extend((f"--{name}", str(value)))
        for name in ("hidden", "verify", "resize-test", "validation"):
            if getattr(args, name.replace("-", "_")):
                command.append(f"--{name}")
        print(f"Running PixelSort on {args.backend} ({args.cuda_mode})...", flush=True)
        return subprocess.run(command, check=False).returncode
    except subprocess.CalledProcessError as error:
        return error.returncode
    except OSError as error:
        raise SystemExit(f"Unable to build or launch Pixel Sort: {error}") from error


if __name__ == "__main__":
    raise SystemExit(main())
