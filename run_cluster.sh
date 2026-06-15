#!/usr/bin/env bash
# ---------------------------------------------------------------------------
# run_cluster.sh - Price a book through a local Thoth cluster (master + slaves).
#
# Launches N local slave servers (ordinary `thoth -server`), then a master
# (`thoth -cluster`) that splits a Monte-Carlo book's paths across them,
# dispatches over HTTP and aggregates the results. Posts the input book to the
# master, writes the result next to the input as <input>.out.yaml, then tears
# everything down. Non-MCL (or non-pricer) books are forwarded whole to the
# first slave, so any book works — only MCL books get their paths split.
#
# Uses the locally built ./build/thoth binary (run `cmake --build build` first).
#
# Usage:
#     ./run_cluster.sh                                  # samples/simple_call.yaml, 2 slaves
#     ./run_cluster.sh <input.yaml>                     # custom input, 2 slaves
#     ./run_cluster.sh <input.yaml> <n_slaves>          # custom slave count
#     ./run_cluster.sh <input.yaml> <n_slaves> <base_port>
#
# Ports: the master listens on <base_port> (default 8090) and the slaves on
# <base_port>+1 .. <base_port>+N. The path-split only kicks in for a single-MCL-
# pricer book; other books are forwarded whole to the first slave.
# ---------------------------------------------------------------------------
set -euo pipefail

# Resolve the project directory (location of this script) so it works from anywhere.
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$ROOT"

INPUT="${1:-samples/simple_call.yaml}"
NSLAVES="${2:-2}"
BASE_PORT="${3:-8090}"
OUTPUT="${INPUT%.yaml}.out.yaml"
THOTH="./build/thoth"

MASTER_PORT="$BASE_PORT"

if [[ ! -x "$THOTH" ]]; then
    echo "error: '$THOTH' not found — build it first: cmake --build build -j" >&2
    exit 1
fi
if [[ ! -f "$INPUT" ]]; then
    echo "error: input file '$INPUT' not found" >&2
    exit 1
fi
if (( NSLAVES < 1 )); then
    echo "error: need at least one slave" >&2
    exit 1
fi

PIDS=()
# Tear down every process we started, whatever the exit path (success, error,
# Ctrl-C). Each thoth instance shares global state, so leaving one running would
# block the next run's port.
cleanup() {
    for pid in "${PIDS[@]:-}"; do
        kill "$pid" >/dev/null 2>&1 || true
    done
    wait >/dev/null 2>&1 || true
}
trap cleanup EXIT INT TERM

# wait until a server answers GET /health (or give up after ~30s). The window is
# generous because a many-slave cluster on one machine starts all servers at once.
wait_health() {
    local url="$1"
    for _ in $(seq 1 150); do
        if curl -sf "$url/health" >/dev/null 2>&1; then
            return 0
        fi
        sleep 0.2
    done
    echo "error: $url did not become healthy in time" >&2
    return 1
}

# --- launch the slaves -----------------------------------------------------
SLAVE_URLS=()
for (( i = 1; i <= NSLAVES; i++ )); do
    port=$(( BASE_PORT + i ))
    echo "==> starting slave $i on port $port"
    "$THOTH" -server "$port" >"/tmp/thoth_slave_${port}.log" 2>&1 &
    PIDS+=( "$!" )
    SLAVE_URLS+=( "http://localhost:${port}" )
done
for url in "${SLAVE_URLS[@]}"; do
    wait_health "$url"
done

# --- launch the master -----------------------------------------------------
echo "==> starting master on port $MASTER_PORT over ${NSLAVES} slave(s)"
"$THOTH" -cluster "$MASTER_PORT" "${SLAVE_URLS[@]}" >"/tmp/thoth_master_${MASTER_PORT}.log" 2>&1 &
PIDS+=( "$!" )
wait_health "http://localhost:${MASTER_PORT}"

# --- price the book through the master -------------------------------------
URL="http://localhost:${MASTER_PORT}/price"
echo "==> POST $INPUT -> $URL  (output: $OUTPUT)"
# Content-Type is REQUIRED: without it curl sends form-urlencoded, which the
# server caps at 8 KB (413 for larger bodies).
curl -sS -X POST -H "Content-Type: application/x-yaml" \
     --data-binary "@${INPUT}" "$URL" > "$OUTPUT"

ABS_OUTPUT="$(cd "$(dirname "$OUTPUT")" && pwd)/$(basename "$OUTPUT")"
echo "==> Done. Result written to $ABS_OUTPUT ($(wc -c < "$OUTPUT") bytes)"
