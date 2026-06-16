#!/usr/bin/env bash
# ---------------------------------------------------------------------------
# run_docker_server.sh - Build the Thoth image and run it as an HTTP server.
#
# Builds the production image (Dockerfile), then runs the container's HTTP
# pricing service, publishing it on the requested host port (8080 inside).
#     POST /price   (YAML body -> YAML result)
#     GET  /health
#
# Pass --gpu to build/run the CUDA variant (mcl_gpu engine on an NVIDIA GPU):
# same Dockerfile, swapped to the nvidia/cuda base images with ENABLE_CUDA=ON,
# and the container gets the host GPU(s) via `docker run --gpus all`. Needs an
# NVIDIA GPU + driver and the NVIDIA Container Toolkit. Books that are not GPU-
# supported (or use any other method) still price on the CPU.
#
# Usage:
#     ./run_docker_server.sh                       # CPU,  host port 8080
#     ./run_docker_server.sh --port 8888           # CPU,  host port 8888
#     ./run_docker_server.sh --gpu                 # GPU,  sm_89 (Ada / RTX 40-series)
#     ./run_docker_server.sh --gpu --arch 86       # GPU,  sm_86 (Ampere / RTX 30-series)
#
# Once up:
#     curl http://localhost:8888/health
#     curl -X POST --data-binary @samples/heston_call.yaml http://localhost:8888/price
#     curl -X POST --data-binary @samples/gpu_call.yaml    http://localhost:8888/price  # --gpu
# ---------------------------------------------------------------------------
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$ROOT"
# shellcheck source=run_docker_common.sh
source "$ROOT/run_docker_common.sh"

PORT=8080
GPU=0
ARCH=89 #!< CUDA compute capability of the target GPU (89 = Ada / RTX 40-series); --gpu only
while [[ $# -gt 0 ]]; do
    case "$1" in
        --port) PORT="$2"; shift 2 ;;
        --port=*) PORT="${1#*=}"; shift ;;
        --gpu) GPU=1; shift ;;
        --arch) ARCH="$2"; shift 2 ;;
        --arch=*) ARCH="${1#*=}"; shift ;;
        -h | --help) echo "usage: $0 [--port <port>] [--gpu] [--arch <cc e.g. 89>]"; exit 0 ;;
        *) echo "error: unknown argument '$1' (usage: $0 [--port <port>] [--gpu] [--arch <cc>])" >&2; exit 1 ;;
    esac
done

# Build the image (thoth_build_image sets THOTH_IMAGE to "thoth" or "thoth-gpu").
gpu_run_flags=()
if [[ $GPU -eq 1 ]]; then
    thoth_build_image --gpu "$ARCH"
    NAME="thoth-server-gpu"
    # --gpus all attaches the host GPU(s); --init makes tini PID 1 so it forwards
    # SIGINT and Ctrl-C tears the container down (thoth installs no signal handler).
    gpu_run_flags=(--init --gpus all)
else
    thoth_build_image
    NAME="thoth-server"
fi

# Replace any previous instance so re-running is idempotent.
docker rm -f "$NAME" >/dev/null 2>&1 || true

echo "==> Server on http://localhost:$PORT  (Ctrl-C to stop)"
# ENTRYPOINT/CMD already run `thoth -server 8080`; map the host port to 8080.
# -t: live single-line progress bar in this console.
exec docker run --rm -t "${gpu_run_flags[@]}" --name "$NAME" -p "$PORT:8080" "$THOTH_IMAGE"
