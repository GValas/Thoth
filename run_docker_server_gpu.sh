#!/usr/bin/env bash
# ---------------------------------------------------------------------------
# run_docker_server_gpu.sh - Build the CUDA Thoth image and run a GPU HTTP server.
#
# Builds the GPU image (Dockerfile.gpu: CUDA toolchain + THOTH_ENABLE_CUDA=ON),
# then runs the HTTP pricing service with the host GPU(s) attached. Books that
# use `method: mcl_gpu` are priced on the GPU; any other method, or a book the
# GPU engine does not support, falls back to the CPU automatically.
#
# Requirements: an NVIDIA GPU + driver and the NVIDIA Container Toolkit, so that
# `docker run --gpus all` works. (On a host without these, use run_docker_server.sh.)
#
# Usage:
#     ./run_docker_server_gpu.sh                       # host port 8080, sm_89 (Ada)
#     ./run_docker_server_gpu.sh --port 8888 --arch 86 # host port 8888, sm_86 (Ampere)
#
# Once up:
#     curl http://localhost:8888/health
#     curl -X POST -H "Content-Type: application/x-yaml" \
#          --data-binary @samples/simple_call.yaml http://localhost:8888/price
# ---------------------------------------------------------------------------
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$ROOT"

PORT=8080
ARCH=89 #!< CUDA compute capability of the target GPU (89 = Ada / RTX 40-series)
while [[ $# -gt 0 ]]; do
    case "$1" in
        --port) PORT="$2"; shift 2 ;;
        --port=*) PORT="${1#*=}"; shift ;;
        --arch) ARCH="$2"; shift 2 ;;
        --arch=*) ARCH="${1#*=}"; shift ;;
        -h | --help) echo "usage: $0 [--port <port>] [--arch <cc e.g. 89>]"; exit 0 ;;
        *) echo "error: unknown argument '$1' (usage: $0 [--port <port>] [--arch <cc>])" >&2; exit 1 ;;
    esac
done

command -v docker >/dev/null 2>&1 || { echo "error: docker not found" >&2; exit 1; }

IMAGE="thoth-gpu"
NAME="thoth-server-gpu"
commit="$(git -C "$ROOT" rev-parse --short HEAD 2>/dev/null || echo unknown)"

echo "==> Building GPU image '$IMAGE' (CUDA sm_$ARCH) at commit $commit ..."
docker build -f Dockerfile.gpu \
    --build-arg GIT_COMMIT="$commit" --build-arg CUDA_ARCH="$ARCH" \
    -t "$IMAGE" "$ROOT"

# Replace any previous instance so re-running is idempotent.
docker rm -f "$NAME" >/dev/null 2>&1 || true

echo "==> GPU server on http://localhost:$PORT  (Ctrl-C to stop)"
# --gpus all attaches the host GPU(s). --init: tini as PID 1 forwards SIGINT so
# Ctrl-C tears the container down (thoth installs no signal handler). -t: live bar.
exec docker run --rm -t --init --gpus all --name "$NAME" -p "$PORT:8080" "$IMAGE"
