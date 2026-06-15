#!/usr/bin/env bash
# ---------------------------------------------------------------------------
# run_docker_batch.sh - Build the Thoth image and price a YAML file in batch.
#
# Builds the production image (Dockerfile), then runs the container in -batch
# mode on the given input. The input's directory is mounted at /data, so the
# result is written next to the input as <script>.out.yaml on the host.
#
# Usage:
#     ./run_docker_batch.sh --input <script.yaml>
#
# Example:
#     ./run_docker_batch.sh --input samples/heston_call.yaml
#     -> writes samples/heston_call.out.yaml
# ---------------------------------------------------------------------------
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$ROOT"
# shellcheck source=run_docker_common.sh
source "$ROOT/run_docker_common.sh"

INPUT=""
while [[ $# -gt 0 ]]; do
    case "$1" in
        --input) INPUT="$2"; shift 2 ;;
        --input=*) INPUT="${1#*=}"; shift ;;
        -h | --help) echo "usage: $0 --input <script.yaml>"; exit 0 ;;
        *) echo "error: unknown argument '$1' (usage: $0 --input <script.yaml>)" >&2; exit 1 ;;
    esac
done

if [[ -z "$INPUT" ]]; then
    echo "usage: $0 --input <script.yaml>" >&2
    exit 1
fi
if [[ ! -f "$INPUT" ]]; then
    echo "error: input file '$INPUT' not found" >&2
    exit 1
fi

# Absolute paths so the input's directory can be bind-mounted at /data.
ABS_INPUT="$(realpath "$INPUT")"
DATA_DIR="$(dirname "$ABS_INPUT")"
IN_NAME="$(basename "$ABS_INPUT")"
OUT_NAME="${IN_NAME%.yaml}.out.yaml" #!< <script>.out.yaml, next to the input

thoth_build_image

echo "==> Pricing $IN_NAME -> $OUT_NAME (in $DATA_DIR)"
# ENTRYPOINT is `thoth`, so the args below are thoth's. -t gives a live progress
# bar; --rm cleans the container up; the dir is mounted read-write for the output.
docker run --rm -t -v "$DATA_DIR:/data" "$THOTH_IMAGE" \
    -batch "/data/$IN_NAME" "/data/$OUT_NAME"

echo "==> Done. Result written to $DATA_DIR/$OUT_NAME"
