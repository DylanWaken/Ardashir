#!/usr/bin/env python3

import subprocess
import sys
from pathlib import Path


def main() -> int:
    source_directory = Path(__file__).resolve().parent.parent
    viewer = source_directory / "Tools" / "ArdaTraceViewer" / "app.py"
    return subprocess.run(
        [sys.executable, str(viewer), *sys.argv[1:]],
        check=False,
    ).returncode


if __name__ == "__main__":
    raise SystemExit(main())
