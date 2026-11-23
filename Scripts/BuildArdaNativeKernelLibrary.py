"""Package compiled native kernels or explicit JIT sources for ArdaBackend."""

import argparse
import json
from pathlib import Path
import struct
import sys

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "Tools" / "ModelExport"))
from ArdaNativeKernelPackage import write_library


def read_entry(entry, base):
    if not isinstance(entry, dict):
        raise ValueError("Each kernel must be an object.")
    key = tuple(entry[field] for field in ("provider", "name", "signature"))
    kernel_format = entry["format"]
    mode = entry.get("mode", "aot")
    if mode not in ("aot", "jit"):
        raise ValueError(f"Unknown kernel mode {mode!r} for {key}.")
    # CUDA C++ compilation is an explicit runtime choice.
    if kernel_format == "cuda-cpp" and mode != "jit":
        raise ValueError(f"CUDA C++ kernel {key} requires mode 'jit'.")
    attributes = entry.get("attributes", [])
    if not isinstance(attributes, list):
        raise ValueError(f"Attributes must be an array for {key}.")
    source = base / entry["file"]
    payload = source.read_bytes()
    if not payload:
        raise ValueError(f"Empty kernel payload: {source}")
    return (*key, kernel_format, entry["entryPoint"], attributes, payload)


def package(manifest, output, base):
    with manifest.open(encoding="utf-8") as file:
        description = json.load(file)
    kernels = description["kernels"]
    if not isinstance(kernels, list) or not kernels:
        raise ValueError("A library needs at least one kernel.")
    write_library(output, description["library"], (read_entry(entry, base) for entry in kernels))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--manifest", required=True, type=Path)
    parser.add_argument("--output", required=True, type=Path)
    parser.add_argument("--base-dir", type=Path, help="Resolve relative payload paths here instead of beside the manifest.")
    args = parser.parse_args()
    try:
        package(args.manifest, args.output, args.base_dir or args.manifest.parent)
    except (OSError, KeyError, TypeError, ValueError, struct.error) as error:
        parser.exit(1, f"{args.manifest}: {error}\n")


if __name__ == "__main__":
    main()
