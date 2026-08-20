#!/usr/bin/env python3

import argparse
import subprocess
from pathlib import Path


def parse_arguments() -> argparse.Namespace:
    source_directory = Path(__file__).resolve().parent.parent
    # Keep the test runner isolated from developer/IDE build trees. CMake build
    # directories are tied to one generator, and reusing a Visual Studio tree
    # from Ninja (or vice versa) leaves FetchContent sub-builds inconsistent.
    default_build = source_directory / "build-tests"

    parser = argparse.ArgumentParser(
        description="Configure, build, and run all Ardashir tests."
    )
    parser.add_argument(
        "build_directory",
        nargs="?",
        type=Path,
        default=default_build,
        help="isolated CMake build directory (default: %(default)s)",
    )
    parser.add_argument("configuration", nargs="?", default="Debug")
    return parser.parse_args()


def main() -> int:
    arguments = parse_arguments()
    source_directory = Path(__file__).resolve().parent.parent
    build_directory = arguments.build_directory.resolve()

    print(f'Configuring tests in "{build_directory}"...')
    subprocess.run(
        [
            "cmake",
            "-S",
            str(source_directory),
            "-B",
            str(build_directory),
            "-DARDASHIR_BUILD_TESTS=ON",
            f"-DCMAKE_BUILD_TYPE={arguments.configuration}",
        ],
        check=True,
    )

    print(f'Building tests using configuration "{arguments.configuration}"...')
    subprocess.run(
        [
            "cmake",
            "--build",
            str(build_directory),
            "--config",
            arguments.configuration,
            "--parallel",
        ],
        check=True,
    )

    print("Running all project tests...")
    return subprocess.run(
        [
            "ctest",
            "--test-dir",
            str(build_directory),
            "-C",
            arguments.configuration,
            "--output-on-failure",
        ],
        check=False,
    ).returncode


if __name__ == "__main__":
    raise SystemExit(main())
