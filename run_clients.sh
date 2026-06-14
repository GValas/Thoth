#!/usr/bin/env bash
# ---------------------------------------------------------------------------
# run_clients.sh - Fire several Thoth pricing clients in parallel
#
# Posts the SAME input file to a running Thoth HTTP server N times
# concurrently, writing each response to an index-suffixed output file
# (<input>.<i>.out.yaml). Useful to exercise the server's request queue
# (it serialises pricing and logs queued client IPs).
#
# The server must already be running (see run_serve.sh or `thoth -server`).
#
# Usage:
#     ./run_clients.sh                              # input=samples/simple_call.yaml, 10 clients
#     ./run_clients.sh <input.yaml>                 # custom input, 10 clients
#     ./run_clients.sh <input.yaml> <count> [exec_name] [port]
#
# Examples:
#     ./run_clients.sh samples/quanto_call.yaml 10
#     ./run_clients.sh samples/simple_call.yaml 5 simple_call_pricing 8080
# ---------------------------------------------------------------------------
set -euo pipefail

# Resolve the project directory (location of this script) so it works from anywhere.
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$ROOT"

INPUT="${1:-samples/simple_call.yaml}"
COUNT="${2:-10}"
EXEC_NAME="${3:-}"            # optional: name of the object to price (X-Exec-Name)
PORT="${4:-8080}"
URL="http://localhost:${PORT}/price"

if [[ ! -f "$INPUT" ]]; then
    echo "error: input file '$INPUT' not found" >&2
    exit 1
fi

echo "==> launching $COUNT parallel clients : $INPUT -> $URL"

pids=()
outs=()
for (( i = 0; i < COUNT; i++ )); do
    # Index the output between the basename and the .out.yaml suffix.
    OUTPUT="${INPUT%.yaml}.${i}.out.yaml"

    # Content-Type is REQUIRED: without it curl defaults to
    # 'application/x-www-form-urlencoded', which the server caps at 8 KB (413).
    CURL_ARGS=( -sS -X POST -H "Content-Type: application/x-yaml" --data-binary "@${INPUT}" )
    if [[ -n "$EXEC_NAME" ]]; then
        CURL_ARGS+=( -H "X-Exec-Name: ${EXEC_NAME}" )
    fi

    curl "${CURL_ARGS[@]}" "$URL" > "$OUTPUT" &
    pids+=( "$!" )
    outs+=( "$OUTPUT" )
    echo "    client #${i} (pid $!) -> $(cd "$(dirname "$OUTPUT")" && pwd)/$(basename "$OUTPUT")"
done

# Wait for every client; report any failure but keep waiting for the rest.
fail=0
for i in "${!pids[@]}"; do
    if ! wait "${pids[$i]}"; then
        echo "    client #${i} FAILED (${outs[$i]})" >&2
        fail=1
    fi
done

echo "==> done : $COUNT clients"
exit "$fail"
