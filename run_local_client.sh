#!/usr/bin/env bash
# ---------------------------------------------------------------------------
# run_local_client.sh - Submit one YAML script to a running Thoth server.
#
# POSTs the input YAML to a Thoth HTTP server's /price endpoint and writes the YAML
# response. The server must already be running (see run_docker_server.sh or
# `thoth -server`).
#
# Usage:
#     ./run_local_client.sh <input.yaml> [output.yaml] [--port <port>] [--exec-name <name>]
#
# The output argument is optional; it defaults to <input>.out.yaml next to the input.
#
# Examples:
#     ./run_local_client.sh samples/simple_call.yaml                 # default port 8080
#     ./run_local_client.sh samples/simple_call.yaml /tmp/r.yaml --port 8888
# ---------------------------------------------------------------------------
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
# NB: do NOT cd into $ROOT — the input path is given relative to the caller's
# working directory (e.g. `./Thoth/samples/x.yaml` from the repo's parent), so
# resolving it must happen from where the user is, not from the script's dir.
# shellcheck source=run_docker_common.sh
source "$ROOT/run_docker_common.sh" #!< require_input_file / default_output

PORT=8080
EXEC_NAME=""          #!< optional: name of the object to price (X-Exec-Name)
INPUT=""
OUTPUT=""

usage() { echo "usage: $0 <input.yaml> [output.yaml] [--port <port>] [--exec-name <name>]"; }

# Flags may appear anywhere; the first two non-flag tokens are input then output.
while [[ $# -gt 0 ]]; do
    case "$1" in
        --port)        PORT="$2"; shift 2 ;;
        --port=*)      PORT="${1#*=}"; shift ;;
        --exec-name)   EXEC_NAME="$2"; shift 2 ;;
        --exec-name=*) EXEC_NAME="${1#*=}"; shift ;;
        -h | --help)   usage; exit 0 ;;
        -*) echo "error: unknown argument '$1'" >&2; usage >&2; exit 1 ;;
        *)
            if [[ -z "$INPUT" ]]; then INPUT="$1"
            elif [[ -z "$OUTPUT" ]]; then OUTPUT="$1"
            else echo "error: unexpected argument '$1'" >&2; usage >&2; exit 1
            fi
            shift ;;
    esac
done

if ! require_input_file "$INPUT" >/dev/null; then usage >&2; exit 1; fi #!< validate (path used as-is below)
[[ -z "$OUTPUT" ]] && OUTPUT="$(default_output "$INPUT")"
URL="http://localhost:${PORT}/price"

echo "==> submitting $INPUT -> $URL"

# Content-Type is REQUIRED: without it curl defaults to
# 'application/x-www-form-urlencoded', which the server caps at 8 KB (413).
# -f makes curl fail (non-zero) on HTTP errors instead of writing the error body.
CURL_ARGS=( -fsS -X POST -H "Content-Type: application/x-yaml" --data-binary "@${INPUT}" )
if [[ -n "$EXEC_NAME" ]]; then
    CURL_ARGS+=( -H "X-Exec-Name: ${EXEC_NAME}" )
fi

curl "${CURL_ARGS[@]}" "$URL" > "$OUTPUT"

echo "==> wrote $(cd "$(dirname "$OUTPUT")" && pwd)/$(basename "$OUTPUT")"
