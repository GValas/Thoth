#!/usr/bin/env bash
# ---------------------------------------------------------------------------
# serve.sh - Run Thoth as an HTTP pricing server inside Docker
#
# Builds the production image (see Dockerfile) and runs the container, which
# defaults to the HTTP pricing service:
#     POST /price   (YAML body -> YAML result)
#     GET  /health
#
# Usage:
#     ./serve.sh              # listen on the default port (8080)
#     ./serve.sh <port>       # publish on a specific host port
#
# Example requests once the server is up:
#     curl http://localhost:8080/health
#     curl -X POST --data-binary @samples/input.yaml http://localhost:8080/price
#     # select a specific named object:
#     curl -X POST -H "X-Exec-Name: <name>" \
#          --data-binary @samples/input.yaml http://localhost:8080/price
# ---------------------------------------------------------------------------
set -euo pipefail

# Resolve the project directory (location of this script) so it works from anywhere.
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$ROOT"

IMAGE="thoth"
PORT="${1:-8080}"
NAME="thoth-server"

# Build the image. The git commit is baked in (and printed at the server's
# startup banner) so it is obvious which source the container runs — if the
# banner shows a different commit than `git rev-parse --short HEAD`, the image is
# stale (Docker layer cache invalidates automatically when src/ changes).
GIT_COMMIT="$(git rev-parse --short HEAD 2>/dev/null || echo unknown)"
echo "==> Building image '$IMAGE' at commit $GIT_COMMIT ..."
docker build --build-arg GIT_COMMIT="$GIT_COMMIT" -t "$IMAGE" .

# Replace any previous instance so re-running the script is idempotent.
docker rm -f "$NAME" >/dev/null 2>&1 || true

echo "==> Running: docker run -t -p $PORT:8080 $IMAGE (container '$NAME')"
# The container's ENTRYPOINT/CMD already start: thoth -server 8080
# --rm: clean up on exit, -p maps host:container, listen on 8080 inside.
# -t: allocate a TTY so the pricing progress bar redraws live as a single
#     in-place line in this attached console (without a live TTY the server
#     falls back to one bar line every 10%).
exec docker run --rm -t --name "$NAME" -p "$PORT:8080" "$IMAGE"
