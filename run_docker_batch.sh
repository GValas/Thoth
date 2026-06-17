#!/usr/bin/env bash
# ---------------------------------------------------------------------------
# run_docker_batch.sh - Build the Thoth image and price a YAML file in batch.
#
# Builds the production image (Dockerfile), then runs the container in -batch mode
# on the given input. The input's directory is bind-mounted read-only and the
# output's directory read-write, so the result can be written anywhere.
#
# Usage:
#     ./run_docker_batch.sh <input.yaml> [output.yaml]
#
# The output argument is optional; it defaults to <input>.out.yaml next to the input.
#
# Examples:
#     ./run_docker_batch.sh samples/simple_call.yaml         # -> samples/simple_call.out.yaml
#     ./run_docker_batch.sh samples/simple_call.yaml /tmp/r.yaml
# ---------------------------------------------------------------------------
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$ROOT"
# shellcheck source=run_docker_common.sh
source "$ROOT/run_docker_common.sh"

usage() { echo "usage: $0 <input.yaml> [output.yaml]"; }
[[ "${1:-}" == "-h" || "${1:-}" == "--help" ]] && { usage; exit 0; }

if ! ABS_INPUT="$(require_input_file "${1:-}")"; then usage >&2; exit 1; fi #!< validate + absolutise
OUTPUT="${2:-$(default_output "$1")}"
ABS_OUTPUT="$(realpath -m "$OUTPUT")"        #!< -m: output need not exist yet

IN_DIR="$(dirname "$ABS_INPUT")";  IN_NAME="$(basename "$ABS_INPUT")"
OUT_DIR="$(dirname "$ABS_OUTPUT")"; OUT_NAME="$(basename "$ABS_OUTPUT")"
mkdir -p "$OUT_DIR"

thoth_build_image

echo "==> Pricing $IN_NAME -> $OUT_NAME"
# ENTRYPOINT is `thoth`, so the args below are thoth's. -t gives a live progress bar;
# --rm cleans the container up. The input dir is mounted read-only at /in and the
# output dir read-write at /out. --user maps the container to the invoking host user
# so the written result is owned by us (not root) and a later host-side run can
# overwrite it instead of failing with "Permission denied".
docker run --rm -t --user "$(id -u):$(id -g)" \
    -v "$IN_DIR:/in:ro" -v "$OUT_DIR:/out" "$THOTH_IMAGE" \
    -batch "/in/$IN_NAME" "/out/$OUT_NAME"

echo "==> Done. Result written to $ABS_OUTPUT"
