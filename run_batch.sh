#!/usr/bin/env bash
# ---------------------------------------------------------------------------
# run_batch.sh - Run Thoth in batch mode on samples/input.yaml
#
# The executable expects (see Thoth.cpp / DisplayHelp):
#     thoth -batch <input.yaml> <output.yaml> [exec_name]
#
# Usage:
#     ./run_batch.sh                 # price using samples/input.yaml (root object)
#     ./run_batch.sh <exec_name>     # run a specific named object from the config
# ---------------------------------------------------------------------------
set -euo pipefail

# Resolve the project directory (location of this script) so it works from anywhere.
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$ROOT"

EXE="build/thoth"
INPUT="samples/input.yaml"
OUTPUT="samples/output.yaml"
EXEC_NAME="${1:-}"   # optional: name of the object to execute

# Build the binary if it is missing.
if [[ ! -x "$EXE" ]]; then
    echo "==> $EXE not found, building..."
    cmake -B build
    cmake --build build -j
fi

if [[ ! -f "$INPUT" ]]; then
    echo "error: input file '$INPUT' not found" >&2
    exit 1
fi

echo "==> Running: $EXE -batch $INPUT $OUTPUT ${EXEC_NAME}"
# passing exec_name selects a specific object; omitting it executes the root.
if [[ -n "$EXEC_NAME" ]]; then
    "$EXE" -batch "$INPUT" "$OUTPUT" "$EXEC_NAME"
else
    "$EXE" -batch "$INPUT" "$OUTPUT"
fi
