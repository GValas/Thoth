#!/usr/bin/env bash
# ---------------------------------------------------------------------------
# client.sh - Send a YAML pricing request to a running Thoth HTTP server
#
# Equivalent of:
#     curl -sS -X POST --data-binary @samples/simple_call.yaml \
#          http://localhost:8080/price > ./samples/simple_call.out.yaml
#
# The server must already be running (see serve.sh).
#
# Usage:
#     ./client.sh                         # input=samples/simple_call.yaml, out=samples/simple_call.out.yaml
#     ./client.sh <input.yaml>            # custom input, output as <input>.out.yaml
#     ./client.sh <input.yaml> <out.yaml> [exec_name] [port]
#
# Examples:
#     ./client.sh samples/quanto_call.yaml         # -> samples/quanto_call.out.yaml
#     ./client.sh samples/simple_call.yaml samples/simple_call.out.yaml simple_call_pricing 8080
# ---------------------------------------------------------------------------
set -euo pipefail

# Resolve the project directory (location of this script) so it works from anywhere.
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$ROOT"

INPUT="${1:-samples/simple_call.yaml}"
# Default output sits next to the input with a ".out" tag before the extension
# (e.g. samples/simple_call.yaml -> samples/simple_call.out.yaml). These are gitignored.
OUTPUT="${2:-${INPUT%.yaml}.out.yaml}"
EXEC_NAME="${3:-}"            # optional: name of the object to price (X-Exec-Name)
PORT="${4:-8080}"
URL="http://localhost:${PORT}/price"

if [[ ! -f "$INPUT" ]]; then
    echo "error: input file '$INPUT' not found" >&2
    exit 1
fi

echo "==> POST $INPUT -> $URL  (output: $OUTPUT)"

# Build curl args. The '@' makes curl send the file CONTENTS as the request body.
# Without it, curl would send the literal path string and the server would reject it.
#
# The Content-Type header is REQUIRED: without it curl defaults to
# 'application/x-www-form-urlencoded', which makes the server (cpp-httplib)
# apply an 8 KB form-payload cap and answer 413 for larger requests.
CURL_ARGS=( -sS -X POST -H "Content-Type: application/x-yaml" --data-binary "@${INPUT}" )
if [[ -n "$EXEC_NAME" ]]; then
    CURL_ARGS+=( -H "X-Exec-Name: ${EXEC_NAME}" )
fi

# '>' overwrites the output file; change to '>>' below if you want to append.
curl "${CURL_ARGS[@]}" "$URL" > "$OUTPUT"

# Report the ABSOLUTE path so there is no ambiguity about where it landed
# (the script runs from the project root, so a relative OUTPUT is rooted there).
ABS_OUTPUT="$(cd "$(dirname "$OUTPUT")" && pwd)/$(basename "$OUTPUT")"
echo "==> Done. Result written to $ABS_OUTPUT"
echo "    ($(wc -c < "$OUTPUT") bytes)"
