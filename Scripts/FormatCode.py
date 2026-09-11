#!/usr/bin/env python3
"""Apply/check the repository's Unreal-style layout with clang-format 19."""

import argparse
import os
from pathlib import Path
import re
import shutil
import subprocess


ROOT = Path(__file__).resolve().parents[1]
SOURCE_ROOTS = ("Source", "Shaders", "Cmake", "Docs/ArdaBackend/Examples")
EXTENSIONS = {".c", ".cc", ".cpp", ".cxx", ".h", ".hpp", ".inl", ".cu", ".cuh", ".hlsl", ".hlsli"}


def find_formatter(explicit):
    if explicit:
        return str(explicit.resolve())
    formatter = shutil.which("clang-format")
    if formatter:
        return formatter
    if os.name == "nt":
        vswhere = Path(os.environ.get("ProgramFiles(x86)", r"C:\Program Files (x86)")) / (
            "Microsoft Visual Studio/Installer/vswhere.exe")
        if vswhere.is_file():
            result = subprocess.run([
                str(vswhere), "-products", "*", "-find",
                r"VC\Tools\Llvm\x64\bin\clang-format.exe", "-utf8",
            ], check=True, capture_output=True, encoding="utf-8-sig")
            candidates = result.stdout.splitlines()
            if candidates:
                return candidates[0]
    raise SystemExit("clang-format 19 is missing. Install LLVM 19 or Visual Studio's LLVM tools, "
                     "or use --clang-format PATH.")


def source_files(paths):
    tracked = subprocess.run(["git", "ls-files", "-z", "--", *SOURCE_ROOTS], cwd=ROOT,
                             check=True, capture_output=True).stdout.decode("utf-8").split("\0")
    files = []
    for name in tracked:
        path = Path(name)
        extension = path.with_suffix("").suffix if path.suffix == ".in" else path.suffix
        if extension in EXTENSIONS:
            files.append(ROOT / path)
    if paths:
        selected = [(ROOT / path).resolve() for path in paths]
        for path in selected:
            if not path.is_relative_to(ROOT):
                raise SystemExit(f"Formatting paths must stay in the repository: {path}")
            if not any(file == path or file.is_relative_to(path) for file in files):
                raise SystemExit(f"No tracked project C++/CUDA/HLSL sources at: {path}")
        files = [file for file in files if any(file == path or file.is_relative_to(path) for path in selected)]
    return files


def formatted_source(formatter, path, source):
    command = [formatter, "--style=file", "--fallback-style=none"]
    result = subprocess.run([*command, "--assume-filename=" + str(path)], input=source,
                            capture_output=True, check=True).stdout
    if path.suffix in (".hlsl", ".hlsli"):
        # clang-format 19 has no HLSL mode. Its C++ pass handles control-flow
        # braces; the C# layout pass understands HLSL's single-bracket attributes
        # and return semantics, keeping successive shader functions separate.
        result = subprocess.run([*command, "--assume-filename=" + str(path.with_suffix(".cs"))],
                                input=result, capture_output=True, check=True).stdout
    return result


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("paths", nargs="*", type=Path, help="Repository-relative files/directories; default: all project sources.")
    parser.add_argument("--check", action="store_true", help="Report formatting differences without modifying files.")
    parser.add_argument("--clang-format", type=Path, help="Path to clang-format 19.")
    args = parser.parse_args(argv)
    try:
        formatter = find_formatter(args.clang_format)
        version = subprocess.run([formatter, "--version"], check=True, capture_output=True, text=True).stdout.strip()
        if not re.search(r"clang-format version 19\.", version):
            raise SystemExit(f"Use clang-format 19 for reproducible output; found {version}.")
        files = source_files(args.paths)
        print(f"{'Checking' if args.check else 'Formatting'} {len(files)} files with {version}...", flush=True)
        changed = 0
        for path in files:
            source = path.read_bytes()
            formatted = formatted_source(formatter, path, source)
            # Compare output directly: clang-format 19's --dry-run can report
            # no-op replacements for SeparateDefinitionBlocks with tab indentation.
            if formatted != source:
                changed += 1
                if args.check:
                    print(f"Needs formatting: {path.relative_to(ROOT)}")
                else:
                    path.write_bytes(formatted)
        print(f"{changed} file(s) {'need formatting' if args.check else 'updated'}.")
        return 1 if args.check and changed else 0
    except subprocess.CalledProcessError as error:
        if error.stderr:
            print(error.stderr.decode("utf-8", errors="replace") if isinstance(error.stderr, bytes) else error.stderr)
        return error.returncode
    except OSError as error:
        raise SystemExit(f"Unable to format project sources: {error}") from error


if __name__ == "__main__":
    raise SystemExit(main())
