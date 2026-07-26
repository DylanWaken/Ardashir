#!/usr/bin/env sh
set -eu

SCRIPT_DIRECTORY=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
SOURCE_DIRECTORY=$(CDPATH= cd -- "$SCRIPT_DIRECTORY/.." && pwd)

python3 "$SOURCE_DIRECTORY/Tools/ArdaTraceViewer/app.py" "$@"
