#!/usr/bin/env python3
"""Fetch a clean Ardashir checkout and build the Unreal-compatible shared library.

The checkout is never patched. Land adaptation fixes in the upstream Ardashir
repository, push them, then pass --update here. Generated files stay under the
plugin's Intermediate directory. Run this before invoking UnrealBuildTool.
"""

from __future__ import annotations

import argparse
import json
import os
from pathlib import Path
import subprocess
import sys


PLUGIN = Path(__file__).resolve().parents[1]
SOURCE = PLUGIN / "ThirdParty" / "Ardashir"
BUILD = PLUGIN / "Intermediate" / "Ardashir" / "Win64"
LOCK = PLUGIN / "ArdaRevision.json"


def run(arguments: list[str], *, cwd: Path = PLUGIN, capture: bool = False) -> str:
    print("+ " + subprocess.list2cmdline(arguments), flush=True)
    result = subprocess.run(arguments, cwd=cwd, check=True, text=True,
                            stdout=subprocess.PIPE if capture else None)
    return result.stdout.strip() if capture else ""


def repository_identity(url: str) -> str:
    # GitHub accepts the same repository over SSH and HTTPS.
    return url.lower().removesuffix(".git").replace("git@github.com:", "https://github.com/", 1)


def verify_checkout(upstream: str) -> None:
    if not (SOURCE / ".git").exists():
        raise RuntimeError(f"{SOURCE} must be an independent upstream git checkout.")
    origin = run(["git", "remote", "get-url", "origin"], cwd=SOURCE, capture=True)
    if repository_identity(origin) != repository_identity(upstream):
        raise RuntimeError(f"Unexpected Ardashir origin: {origin}")
    changes = run(["git", "status", "--porcelain", "--untracked-files=all"],
                  cwd=SOURCE, capture=True)
    if changes:
        raise RuntimeError("The dependency checkout has local changes. Move fixes to "
                           "the upstream repository, push them, and update this checkout.\n" + changes)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--update", action="store_true",
                        help="Fast-forward the clean dependency to latest upstream before building")
    parser.add_argument("--ref", help="Use an upstream commit/tag instead of the tracked branch (no --update)")
    parser.add_argument("--configure-only", action="store_true")
    parser.add_argument("--test", action="store_true",
                        help="Build and run CPU-only shared-library ABI regression checks")
    parser.add_argument("--generator", default="Visual Studio 17 2022")
    parser.add_argument("--toolset", help="CMake VS toolset, e.g. version=14.44.35207")
    parser.add_argument("--dependency-cache", type=Path,
                        help="Reuse downloaded GLFW, DXC and EABase sources from another CMake _deps directory")
    parser.add_argument("--parallel", type=int, default=max(1, min(os.cpu_count() or 1, 16)))
    args = parser.parse_args()
    if os.name != "nt":
        parser.error("The Unreal provider build currently supports Windows x64 only.")
    if args.ref and args.update:
        parser.error("--ref and --update are mutually exclusive")
    lock = json.loads(LOCK.read_text(encoding="utf-8"))
    upstream = lock["upstream"]
    pin = lock["revision"]
    branch = lock["branch"]
    if not SOURCE.exists():
        SOURCE.parent.mkdir(parents=True, exist_ok=True)
        run(["git", "clone", "--branch", branch, "--recurse-submodules", upstream, str(SOURCE)])
    verify_checkout(upstream)
    if args.ref:
        run(["git", "fetch", "origin", args.ref], cwd=SOURCE)
        run(["git", "checkout", "--detach", "FETCH_HEAD"], cwd=SOURCE)
    elif args.update:
        run(["git", "fetch", "origin", f"refs/heads/{branch}"], cwd=SOURCE)
        run(["git", "merge-base", "--is-ancestor", "HEAD", "FETCH_HEAD"], cwd=SOURCE)
        run(["git", "checkout", "--detach", "FETCH_HEAD"], cwd=SOURCE)
    elif run(["git", "rev-parse", "HEAD"], cwd=SOURCE, capture=True) != pin:
        if subprocess.run(["git", "cat-file", "-e", f"{pin}^{{commit}}"], cwd=SOURCE,
                          stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL).returncode:
            run(["git", "fetch", "origin", pin], cwd=SOURCE)
        run(["git", "checkout", "--detach", pin], cwd=SOURCE)
    gitlinks = run(["git", "ls-tree", "HEAD", "ThirdParty/"], cwd=SOURCE, capture=True)
    populated = True
    for entry in gitlinks.splitlines():
        metadata, name = entry.split("\t", 1)
        mode, kind, expected = metadata.split()
        if mode != "160000":
            continue
        directory = SOURCE / name
        if not (directory / ".git").exists() or run(
            ["git", "rev-parse", "HEAD"], cwd=directory, capture=True) != expected:
            populated = False
            break
    if not populated:
        run(["git", "submodule", "update", "--init", "--recursive"], cwd=SOURCE)
    verify_checkout(upstream)
    revision = run(["git", "rev-parse", "HEAD"], cwd=SOURCE, capture=True)
    configure = ["cmake", "-S", str(PLUGIN / "Cmake"), "-B", str(BUILD),
                 "-G", args.generator, "-A", "x64", f"-DARDA_UNREAL_SOURCE={SOURCE}"]
    if args.toolset:
        configure.extend(["-T", args.toolset])
    if args.dependency_cache:
        cache = args.dependency_cache.resolve()
        for name in ("ardashir_glfw", "ardashir_dxc", "eabase"):
            path = cache / (name + "-src")
            if not path.is_dir():
                raise RuntimeError(f"Dependency cache is missing {path}")
            configure.append(f"-DFETCHCONTENT_SOURCE_DIR_{name.upper()}={path}")
    manifest_path = BUILD / "ArdaBuildManifest.json"
    manifest_path.unlink(missing_ok=True)
    run(configure)
    if not args.configure_only:
        run(["cmake", "--build", str(BUILD), "--config", "RelWithDebInfo",
             "--target", "ArdaUnrealCore", "--parallel", str(args.parallel)])
        if args.test:
            run(["cmake", "--build", str(BUILD), "--config", "RelWithDebInfo",
                 "--target", "ArdaUnrealAbiTests", "--parallel", str(args.parallel)])
            run([str(BUILD / "bin" / "RelWithDebInfo" / "ArdaUnrealAbiTests.exe")])
        manifest = {
            "upstream": upstream,
            "revision": revision,
            "configuration": "RelWithDebInfo",
            "runtime": "MultiThreadedDLL",
            "iterator_debug_level": 0,
            "eastl_debug": 0,
            "rtti": True,
            "exceptions": True,
            "generator": args.generator,
            "toolset": args.toolset,
        }
        manifest_path.write_text(
            json.dumps(manifest, indent=2) + "\n", encoding="utf-8")
    if args.ref or args.update:
        lock["revision"] = revision
        LOCK.write_text(json.dumps(lock, indent=2) + "\n", encoding="utf-8")
    verify_checkout(upstream)
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (OSError, RuntimeError, subprocess.CalledProcessError) as error:
        print(f"Ardashir Unreal build failed: {error}", file=sys.stderr)
        raise SystemExit(1)
