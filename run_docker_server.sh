#!/usr/bin/env bash
# ---------------------------------------------------------------------------
# run_docker_server.sh - Build the Thoth image and serve it over HTTP.
#
# Builds the production image (Dockerfile), then runs the container's HTTP pricing
# service, publishing it on the requested host port (8080 inside):
#     POST /price   (YAML body -> YAML result)
#     GET  /health
#
# Two modes, selected by --slaves:
#   * default (no --slaves): a single server container.
#   * --slaves N: a cluster of N detached slave containers plus a foreground master,
#     all on a private Docker network so they reach each other by container name.
#     Only the master's port is published; it splits a single-MCL-pricer book's paths
#     across the slaves (other books are forwarded whole to one slave). Ctrl-C (or the
#     master exiting) tears the whole cluster — master, slaves and network — down.
#
# --gpu builds/runs the CUDA variant (mcl_gpu engine on NVIDIA GPUs): same Dockerfile
# swapped to the nvidia/cuda base with ENABLE_CUDA=ON, with --gpus all attached to
# every container. Needs an NVIDIA GPU + driver and the NVIDIA Container Toolkit.
# Books that are not GPU-supported still price on the CPU.
#
# Usage:
#     ./run_docker_server.sh [--gpu] [--slaves <n>] [--port <port>] [--arch <cc>]
#
# Examples:
#     ./run_docker_server.sh                       # single CPU server, host port 8080
#     ./run_docker_server.sh --port 7777           # single CPU server, host port 7777
#     ./run_docker_server.sh --slaves 10           # cluster: 10 slaves + master
#     ./run_docker_server.sh --gpu --slaves 4      # GPU cluster, sm_89
#     ./run_docker_server.sh --gpu --arch 86       # single GPU server, sm_86 (Ampere)
#
# Once up:
#     curl http://localhost:8080/health
#     curl -X POST --data-binary @samples/simple_call.yaml http://localhost:8080/price
# ---------------------------------------------------------------------------
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$ROOT"
# shellcheck source=run_docker_common.sh
source "$ROOT/run_docker_common.sh"

PORT=8080
GPU=0
ARCH=89   #!< CUDA compute capability of the target GPU (89 = Ada / RTX 40-series); --gpu only
SLAVES=0  #!< 0 = single server; >=1 = cluster with that many slave containers
INNER_PORT=8080 #!< every container listens on 8080 inside its own namespace

usage() { echo "usage: $0 [--gpu] [--slaves <n>] [--port <port>] [--arch <cc e.g. 89>]"; }

while [[ $# -gt 0 ]]; do
    case "$1" in
        --port)     PORT="$2"; shift 2 ;;
        --port=*)   PORT="${1#*=}"; shift ;;
        --gpu)      GPU=1; shift ;;
        --slaves)   SLAVES="$2"; shift 2 ;;
        --slaves=*) SLAVES="${1#*=}"; shift ;;
        --arch)     ARCH="$2"; shift 2 ;;
        --arch=*)   ARCH="${1#*=}"; shift ;;
        -h | --help) usage; exit 0 ;;
        *) echo "error: unknown argument '$1'" >&2; usage >&2; exit 1 ;;
    esac
done
if ! [[ "$SLAVES" =~ ^[0-9]+$ ]]; then
    echo "error: --slaves must be a non-negative integer" >&2; usage >&2; exit 1
fi

# Build the image (thoth_build_image sets THOTH_IMAGE to "thoth" or "thoth-gpu").
# --init makes tini PID 1 so it forwards SIGINT and Ctrl-C tears the container down
# (thoth installs no signal handler). --gpus all attaches the host GPU(s).
run_flags=(--init)
if [[ $GPU -eq 1 ]]; then
    thoth_build_image --gpu "$ARCH"
    run_flags+=(--gpus all)
else
    thoth_build_image
fi

# ---- single server --------------------------------------------------------
if (( SLAVES == 0 )); then
    NAME="thoth-server"
    docker rm -f "$NAME" >/dev/null 2>&1 || true   #!< idempotent re-run
    echo "==> Server on http://localhost:$PORT  (Ctrl-C to stop)"
    # ENTRYPOINT/CMD already run `thoth -server 8080`; map the host port to 8080.
    # -t: live single-line progress bar in this console.
    exec docker run --rm -t "${run_flags[@]}" --name "$NAME" -p "$PORT:$INNER_PORT" "$THOTH_IMAGE"
fi

# ---- cluster (master + N slaves) ------------------------------------------
NETWORK="thoth-cluster-net"   #!< private network; slaves resolve by container name
MASTER="thoth-cluster-master"
SLAVE_PREFIX="thoth-cluster-slave"

# Remove the master, every slave and the network. Idempotent: clears a stale run
# before starting and (via the trap) tears the cluster down on exit.
cluster_down() {
    docker rm -f "$MASTER" >/dev/null 2>&1 || true
    for (( i = 1; i <= SLAVES; i++ )); do
        docker rm -f "${SLAVE_PREFIX}-$i" >/dev/null 2>&1 || true
    done
    docker network rm "$NETWORK" >/dev/null 2>&1 || true
}

cluster_down
trap cluster_down EXIT INT TERM
docker network create "$NETWORK" >/dev/null

# Start one detached container per slave; collect their in-network URLs.
urls=()
for (( i = 1; i <= SLAVES; i++ )); do
    name="${SLAVE_PREFIX}-$i"
    echo "==> starting slave container $name (inner $INNER_PORT)"
    docker run -d "${run_flags[@]}" --name "$name" --network "$NETWORK" "$THOTH_IMAGE" \
        -server "$INNER_PORT" >/dev/null
    urls+=( "http://$name:$INNER_PORT" )
done

# Wait until each slave accepts connections. The runtime image has no curl but has
# bash, so probe its own listening socket from inside the container.
for (( i = 1; i <= SLAVES; i++ )); do
    name="${SLAVE_PREFIX}-$i"
    ready=0
    for (( t = 0; t < 300; t++ )); do
        if docker exec "$name" bash -c "exec 3<>/dev/tcp/127.0.0.1/$INNER_PORT" 2>/dev/null; then
            ready=1; break
        fi
        sleep 0.2
    done
    if (( ! ready )); then
        echo "error: slave container $name did not become ready" >&2
        docker logs "$name" >&2 || true
        exit 1
    fi
done
echo "==> all $SLAVES slave container(s) ready"

echo "==> Cluster on http://localhost:$PORT  ($SLAVES slave container(s))  (Ctrl-C to stop)"
# Master in the foreground so it owns this console (live -t progress bar) and Ctrl-C
# reaches it; on exit the trap removes every container and the network. --rm removes
# the master itself; slaves are removed by cluster_down.
docker run --rm -t "${run_flags[@]}" --name "$MASTER" --network "$NETWORK" -p "$PORT:$INNER_PORT" \
    "$THOTH_IMAGE" -cluster "$INNER_PORT" "${urls[@]}"
