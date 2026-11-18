#!/usr/bin/env python3

import argparse
import math
import platform
import subprocess
from pathlib import Path

from ExampleBuild import build_example


def parse_arguments(argv=None, *, denoising=False) -> argparse.Namespace:
    source_directory = Path(__file__).resolve().parents[2]
    default_backend = "d3d12" if platform.system() == "Windows" else "vulkan"
    default_build = source_directory / (
        "build" if platform.system() == "Windows" else "build-linux"
    )
    parser = argparse.ArgumentParser(
        description=(
            "Configure, build, and run Cornell Box with an exported AI denoiser."
            if denoising else "Configure, build, and run the RDG Cornell Box path tracer."
        )
    )
    parser.add_argument(
        "backend", nargs="?", choices=("d3d12", "vulkan"), default=default_backend
    )
    parser.add_argument("build_directory", nargs="?", type=Path, default=default_build)
    parser.add_argument("configuration", nargs="?", default="Debug")
    parser.add_argument("--run-only", action="store_true", help="Skip configuration and compilation.")
    parser.add_argument("--cuda-include-dir", type=Path, help="Build-time CUDA SDK include directory.")
    parser.add_argument(
        "--denoise-package", type=Path, required=denoising,
        help="Exported .ardagpu model descriptor; its static dimensions must match the window.",
    )
    parser.add_argument(
        "--denoise-state-dict", type=Path,
        help="Optional .ardastate checkpoint; defaults to the descriptor's sibling checkpoint.",
    )
    parser.add_argument("--denoise-dump", type=Path, help="Output prefix for noisy and denoised HDR readbacks.")
    parser.add_argument("--frames", type=int)
    parser.add_argument("--hidden", action="store_true")
    parser.add_argument("--validation", action="store_true", help="Enable native GPU validation; requires installed debug layers.")
    parser.add_argument("--fullscreen", action="store_true")
    parser.add_argument("--width", type=int, default=256 if denoising else 1280)
    parser.add_argument("--height", type=int, default=256 if denoising else 720)
    parser.add_argument(
        "--samples-per-dispatch", "--spp", dest="samples_per_dispatch",
        type=int, default=1 if denoising else 32,
        help="Independent paths launched in parallel per pixel (default: %(default)s).",
    )
    parser.add_argument(
        "--max-samples", type=int, default=8 if denoising else 16384 * 4,
        help="Progressive samples per pixel before tracing pauses.",
    )
    parser.add_argument("--max-bounces", type=int, default=12)
    parser.add_argument("--exposure", type=float, default=1.0)
    parser.add_argument("--seed", type=int, default=1)
    parser.add_argument(
        "--no-compaction", action="store_true",
        help="Disable static BLAS compaction even when the backend supports it.",
    )
    return parser.parse_args(argv)


def main(argv=None, *, denoising=False) -> int:
    arguments = parse_arguments(argv, denoising=denoising)
    target = "CornellDenoising" if denoising else "CornellBox"
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
    if not math.isfinite(arguments.exposure) or arguments.exposure <= 0:
        raise SystemExit("--exposure must be positive and finite.")
    if not 0 <= arguments.seed <= 0xFFFFFFFF:
        raise SystemExit("--seed must fit in an unsigned 32-bit integer.")
    if arguments.hidden and arguments.fullscreen:
        raise SystemExit("--hidden and --fullscreen cannot be combined.")
    if (arguments.denoise_state_dict or arguments.denoise_dump) and not arguments.denoise_package:
        raise SystemExit("--denoise-state-dict and --denoise-dump require --denoise-package.")
    for option in ("denoise_package", "denoise_state_dict"):
        path = getattr(arguments, option)
        if path is not None and not path.is_file():
            raise SystemExit(f"--{option.replace('_', '-')} file does not exist: {path}")

    if not arguments.run_only:
        options = ["-DARDASHIR_BUILD_CORNELL_BOX=ON",
                   f"-DARDASHIR_BACKEND_{arguments.backend.upper()}=ON"]
        if arguments.denoise_package:
            options.append("-DARDASHIR_ENABLE_CUDA=ON")
            if not (build_directory / "CMakeCache.txt").is_file():
                # Exported descriptors already contain their CUDA kernels.
                options.append("-DARDASHIR_BUILD_CUDA_KERNELS=OFF")
        if arguments.cuda_include_dir is not None:
            options.append(f"-DARDASHIR_CUDA_INCLUDE_DIR={arguments.cuda_include_dir.resolve()}")
        build_example(target, build_directory, arguments.configuration, options)

    executable_name = target + (".exe" if platform.system() == "Windows" else "")
    candidates = (
        build_directory / "Source" / "ArdaTests" / "Examples" / target
        / arguments.configuration / executable_name,
        build_directory / "Source" / "ArdaTests" / "Examples" / target / executable_name,
    )
    executable = next((path for path in candidates if path.is_file()), None)
    if executable is None:
        raise SystemExit(
            f"Unable to locate {target} under {build_directory}. "
            "Check the build directory/configuration, or omit --run-only to build it."
        )

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
    if arguments.validation:
        run_arguments.append("--validation")
    if arguments.fullscreen:
        run_arguments.append("--fullscreen")
    if arguments.no_compaction:
        run_arguments.append("--no-compaction")
    for option in ("denoise_package", "denoise_state_dict", "denoise_dump"):
        path = getattr(arguments, option)
        if path is not None:
            run_arguments.extend((f"--{option.replace('_', '-')}", str(path.resolve())))
    print(f'Running {target} with backend "{arguments.backend}"...', flush=True)
    return subprocess.run(run_arguments, check=False).returncode


if __name__ == "__main__":
    raise SystemExit(main())
