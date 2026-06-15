#!/usr/bin/env bash
# ---------------------------------------------------------------------------
# run_docker_server.sh - Build the Thoth image and run it as an HTTP server.
#
# Builds the production image (Dockerfile), then runs the container's HTTP
# pricing service, publishing it on the requested host port (8080 inside).
#     POST /price   (YAML body -> YAML result)
#     GET  /health
#
# Usage:
#     ./run_docker_server.sh                 # host port 8080
#     ./run_docker_server.sh --port 8888     # host port 8888
#
# Once up:
#     curl http://localhost:8888/health
#     curl -X POST --data-binary @samples/heston_call.yaml http://localhost:8888/price
# ---------------------------------------------------------------------------
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$ROOT"
# shellcheck source=run_docker_common.sh
source "$ROOT/run_docker_common.sh"

PORT=8080
while [[ $# -gt 0 ]]; do
    case "$1" in
        --port) PORT="$2"; shift 2 ;;
        --port=*) PORT="${1#*=}"; shift ;;
        -h | --help) echo "usage: $0 [--port <port>]"; exit 0 ;;
        *) echo "error: unknown argument '$1' (usage: $0 [--port <port>])" >&2; exit 1 ;;
    esac
done

NAME="thoth-server"
thoth_build_image

# Replace any previous instance so re-running is idempotent.
docker rm -f "$NAME" >/dev/null 2>&1 || true

echo "==> Server on http://localhost:$PORT  (Ctrl-C to stop)"
# ENTRYPOINT/CMD already run `thoth -server 8080`; map the host port to 8080.
# -t: live single-line progress bar in this console.
exec docker run --rm -t --name "$NAME" -p "$PORT:8080" "$THOTH_IMAGE"
