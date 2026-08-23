#!/usr/bin/env python3

import argparse
import platform
import subprocess
from pathlib import Path


def parse_arguments() -> argparse.Namespace:
    source_directory = Path(__file__).resolve().parents[2]
    default_backend = "d3d12" if platform.system() == "Windows" else "vulkan"
    default_build = source_directory / (
        "build" if platform.system() == "Windows" else "build-linux"
    )
    parser = argparse.ArgumentParser(
        description="Configure, build, and run the RDG Cornell Box path tracer."
    )
    parser.add_argument(
        "backend", nargs="?", choices=("d3d12", "vulkan"), default=default_backend
    )
    parser.add_argument("build_directory", nargs="?", type=Path, default=default_build)
    parser.add_argument("configuration", nargs="?", default="Debug")
    parser.add_argument("--frames", type=int)
    parser.add_argument("--hidden", action="store_true")
    parser.add_argument("--fullscreen", action="store_true")
    parser.add_argument("--width", type=int, default=1280)
    parser.add_argument("--height", type=int, default=720)
    parser.add_argument(
        "--samples-per-dispatch", "--spp", dest="samples_per_dispatch",
        type=int, default=32,
        help="Independent paths launched in parallel per pixel (default: 8).",
    )
    parser.add_argument(
        "--max-samples", type=int, default=16384 * 4,
        help="Progressive samples per pixel before tracing pauses.",
    )
    parser.add_argument("--max-bounces", type=int, default=12)
    parser.add_argument("--exposure", type=float, default=1.0)
    parser.add_argument("--seed", type=int, default=1)
    parser.add_argument(
        "--no-compaction", action="store_true",
        help="Disable static BLAS compaction even when the backend supports it.",
    )
    return parser.parse_args()


def main() -> int:
    arguments = parse_arguments()
    source_directory = Path(__file__).resolve().parents[2]
    build_directory = arguments.build_directory.resolve()
    if arguments.frames is not None and arguments.frames < 0:
        raise SystemExit("--frames must be non-negative.")
    if arguments.width <= 0 or arguments.height <= 0:
        raise SystemExit("--width and --height must be positive.")
    if not 1 <= arguments.samples_per_dispatch <= 64:
        raise SystemExit("--samples-per-dispatch must be in [1, 64].")
    if arguments.max_samples <= 0:
        raise SystemExit("--max-samples must be positive.")
    if not 1 <= arguments.max_bounces <= 32:
        raise SystemExit("--max-bounces must be in [1, 32].")
    if arguments.exposure <= 0:
        raise SystemExit("--exposure must be positive.")
    if not 0 <= arguments.seed <= 0xFFFFFFFF:
        raise SystemExit("--seed must fit in an unsigned 32-bit integer.")
    if arguments.hidden and arguments.fullscreen:
        raise SystemExit("--hidden and --fullscreen cannot be combined.")

    print(f'Configuring CornellBox in "{build_directory}"...')
    subprocess.run(
        [
            "cmake", "-S", str(source_directory), "-B", str(build_directory),
            "-DARDASHIR_BUILD_CORNELL_BOX=ON",
            f"-DCMAKE_BUILD_TYPE={arguments.configuration}",
        ],
        check=True,
    )
    print(f'Building CornellBox ({arguments.configuration})...')
    subprocess.run(
        [
            "cmake", "--build", str(build_directory),
            "--config", arguments.configuration,
            "--target", "CornellBox", "--parallel",
        ],
        check=True,
    )

    executable_name = "CornellBox.exe" if platform.system() == "Windows" else "CornellBox"
    candidates = (
        build_directory / "Source" / "ArdaTests" / "CornellBox"
        / arguments.configuration / executable_name,
        build_directory / "Source" / "ArdaTests" / "CornellBox" / executable_name,
    )
    executable = next((path for path in candidates if path.is_file()), None)
    if executable is None:
        raise SystemExit("Unable to locate the CornellBox executable.")

    run_arguments = [
        str(executable),
        "--backend", arguments.backend,
        "--width", str(arguments.width),
        "--height", str(arguments.height),
        "--samples-per-dispatch", str(arguments.samples_per_dispatch),
        "--max-samples", str(arguments.max_samples),
        "--max-bounces", str(arguments.max_bounces),
        "--exposure", str(arguments.exposure),
        "--seed", str(arguments.seed),
    ]
    if arguments.frames is not None:
        run_arguments.extend(("--frames", str(arguments.frames)))
    if arguments.hidden:
        run_arguments.append("--hidden")
    if arguments.fullscreen:
        run_arguments.append("--fullscreen")
    if arguments.no_compaction:
        run_arguments.append("--no-compaction")
    print(f'Running CornellBox with backend "{arguments.backend}"...')
    return subprocess.run(run_arguments, check=False).returncode


if __name__ == "__main__":
    raise SystemExit(main())
