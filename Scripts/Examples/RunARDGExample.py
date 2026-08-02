#!/usr/bin/env python3

import argparse
import platform
import subprocess
from pathlib import Path


def parse_arguments() -> argparse.Namespace:
    source_directory = Path(__file__).resolve().parents[2]
    default_backend = "d3d12" if platform.system() == "Windows" else "vulkan"
    default_build = (
        source_directory / ("build" if platform.system() == "Windows" else "build-linux")
    )

    parser = argparse.ArgumentParser(
        description="Configure, build, and run the ARDG terrain example."
    )
    parser.add_argument(
        "backend",
        nargs="?",
        choices=("d3d12", "vulkan"),
        default=default_backend,
    )
    parser.add_argument(
        "build_directory",
        nargs="?",
        type=Path,
        default=default_build,
    )
    parser.add_argument("configuration", nargs="?", default="Debug")
    parser.add_argument("--frames", type=int)
    parser.add_argument("--hidden", action="store_true")
    parser.add_argument("--fullscreen", action="store_true")
    parser.add_argument("--width", type=int, default=1920)
    parser.add_argument("--height", type=int, default=1080)
    return parser.parse_args()


def main() -> int:
    arguments = parse_arguments()
    source_directory = Path(__file__).resolve().parents[2]
    build_directory = arguments.build_directory.resolve()

    if arguments.frames is not None and arguments.frames < 0:
        raise SystemExit("--frames must be non-negative.")
    if arguments.width <= 0 or arguments.height <= 0:
        raise SystemExit("--width and --height must be positive.")
    if arguments.hidden and arguments.fullscreen:
        raise SystemExit("--hidden and --fullscreen cannot be combined.")

    print(f'Configuring ARDGExample in "{build_directory}"...')
    subprocess.run(
        [
            "cmake",
            "-S",
            str(source_directory),
            "-B",
            str(build_directory),
            "-DARDASHIR_BUILD_ARDG_EXAMPLE=ON",
            f"-DCMAKE_BUILD_TYPE={arguments.configuration}",
        ],
        check=True,
    )

    print(f'Building ARDGExample using configuration "{arguments.configuration}"...')
    subprocess.run(
        [
            "cmake",
            "--build",
            str(build_directory),
            "--config",
            arguments.configuration,
            "--target",
            "ARDGExample",
            "--parallel",
        ],
        check=True,
    )

    executable_name = "ARDGExample.exe" if platform.system() == "Windows" else "ARDGExample"
    executable_candidates = (
        build_directory
        / "Source"
        / "ArdaTests"
        / "ARDGExample"
        / arguments.configuration
        / executable_name,
        build_directory / "Source" / "ArdaTests" / "ARDGExample" / executable_name,
    )
    executable = next(
        (candidate for candidate in executable_candidates if candidate.is_file()),
        None,
    )
    if executable is None:
        raise SystemExit("Unable to locate the ARDGExample executable.")

    run_arguments = [
        str(executable),
        "--backend",
        arguments.backend,
        "--width",
        str(arguments.width),
        "--height",
        str(arguments.height),
    ]
    if arguments.frames is not None:
        run_arguments.extend(("--frames", str(arguments.frames)))
    if arguments.hidden:
        run_arguments.append("--hidden")
    if arguments.fullscreen:
        run_arguments.append("--fullscreen")

    print(f'Running ARDGExample with backend "{arguments.backend}"...')
    return subprocess.run(run_arguments, check=False).returncode


if __name__ == "__main__":
    raise SystemExit(main())
