#!/usr/bin/env bash
# ---------------------------------------------------------------------------
# run_local_client.sh - Submit one YAML script to a running Thoth server.
#
# POSTs the input YAML to a Thoth HTTP server's /price endpoint and writes the
# YAML response next to the input as <script>.out.yaml (i.e. the .yaml suffix
# is replaced with .out.yaml).
#
# The server must already be running (see run_docker_server.sh or `thoth
# -server`).
#
# Usage:
#     ./run_local_client.sh --input <script.yaml> [--port <port>] [--exec-name <name>]
#
# Examples:
#     ./run_local_client.sh --port 8888 --input samples/simple_call.yaml
#     ./run_local_client.sh --input samples/heston_call.yaml      # default port 8080
# ---------------------------------------------------------------------------
set -euo pipefail

# Resolve the project directory (location of this script) so it works from anywhere.
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$ROOT"

PORT=8080
INPUT=""
EXEC_NAME=""            # optional: name of the object to price (X-Exec-Name)

usage() { echo "usage: $0 --input <script.yaml> [--port <port>] [--exec-name <name>]"; }

while [[ $# -gt 0 ]]; do
    case "$1" in
        --port)        PORT="$2"; shift 2 ;;
        --port=*)      PORT="${1#*=}"; shift ;;
        --input)       INPUT="$2"; shift 2 ;;
        --input=*)     INPUT="${1#*=}"; shift ;;
        --exec-name)   EXEC_NAME="$2"; shift 2 ;;
        --exec-name=*) EXEC_NAME="${1#*=}"; shift ;;
        -h | --help)   usage; exit 0 ;;
        *) echo "error: unknown argument '$1'" >&2; usage >&2; exit 1 ;;
    esac
done

if [[ -z "$INPUT" ]]; then
    echo "error: --input <script.yaml> is required" >&2
    usage >&2
    exit 1
fi
if [[ ! -f "$INPUT" ]]; then
    echo "error: input file '$INPUT' not found" >&2
    exit 1
fi

# Replace the .yaml suffix with .out.yaml (falls back to appending if none).
if [[ "$INPUT" == *.yaml ]]; then
    OUTPUT="${INPUT%.yaml}.out.yaml"
else
    OUTPUT="${INPUT}.out.yaml"
fi
URL="http://localhost:${PORT}/price"

echo "==> submitting $INPUT -> $URL"

# Content-Type is REQUIRED: without it curl defaults to
# 'application/x-www-form-urlencoded', which the server caps at 8 KB (413).
# -f makes curl fail (non-zero) on HTTP errors instead of writing the error
# body into the .out.yaml.
CURL_ARGS=( -fsS -X POST -H "Content-Type: application/x-yaml" --data-binary "@${INPUT}" )
if [[ -n "$EXEC_NAME" ]]; then
    CURL_ARGS+=( -H "X-Exec-Name: ${EXEC_NAME}" )
fi

curl "${CURL_ARGS[@]}" "$URL" > "$OUTPUT"

echo "==> wrote $(cd "$(dirname "$OUTPUT")" && pwd)/$(basename "$OUTPUT")"
