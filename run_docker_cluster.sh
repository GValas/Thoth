#!/usr/bin/env bash
# ---------------------------------------------------------------------------
# run_docker_cluster.sh - Build the Thoth image and run a cluster of containers.
#
# Builds the production image (Dockerfile), then brings up a small cluster the
# way docker-compose would: ONE container per slave plus one for the master, all
# on a private Docker network so they reach each other by container name.
#   * N slaves : `thoth-cluster-slave-1 .. -N`, each a `thoth -server 8080`
#                container. They are reachable only on the cluster network
#                (their 8080 is NOT published to the host).
#   * 1 master : `thoth-cluster-master`, a `thoth -cluster 8080 http://...:8080`
#                container in the foreground that splits an MCL pricing's paths
#                across the slaves. Only the master's 8080 is published, on --port.
# Ctrl-C (or the master exiting) tears the whole cluster down: master, every
# slave container, and the network are removed on exit.
#
# Usage:
#     ./run_docker_cluster.sh --port 8888 --slaves 10
#
# Then price through the master (single-MCL-pricer books get path-split; other
# books are forwarded whole to one slave):
#     curl -X POST -H "Content-Type: application/x-yaml" \
#          --data-binary @samples/heston_call.yaml http://localhost:8888/price
# ---------------------------------------------------------------------------
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$ROOT"
# shellcheck source=run_docker_common.sh
source "$ROOT/run_docker_common.sh"

PORT=8888
SLAVES=4
while [[ $# -gt 0 ]]; do
    case "$1" in
        --port) PORT="$2"; shift 2 ;;
        --port=*) PORT="${1#*=}"; shift ;;
        --slaves) SLAVES="$2"; shift 2 ;;
        --slaves=*) SLAVES="${1#*=}"; shift ;;
        -h | --help) echo "usage: $0 [--port <port>] [--slaves <n>]"; exit 0 ;;
        *) echo "error: unknown argument '$1' (usage: $0 [--port <port>] [--slaves <n>])" >&2; exit 1 ;;
    esac
done
if ! [[ "$SLAVES" =~ ^[0-9]+$ ]] || (( SLAVES < 1 )); then
    echo "error: --slaves must be a positive integer" >&2; exit 1
fi

NETWORK="thoth-cluster-net"      #!< private network; slaves resolve by container name
MASTER="thoth-cluster-master"
SLAVE_PREFIX="thoth-cluster-slave"
INNER_PORT=8080                  #!< every container listens on 8080 inside its own namespace

# Remove the master, all slave containers, and the network. Idempotent: used both
# to clear any stale run before starting and (via the trap) to tear down on exit.
cluster_down() {
    docker rm -f "$MASTER" >/dev/null 2>&1 || true
    for (( i = 1; i <= SLAVES; i++ )); do
        docker rm -f "${SLAVE_PREFIX}-$i" >/dev/null 2>&1 || true
    done
    docker network rm "$NETWORK" >/dev/null 2>&1 || true
}

thoth_build_image

# Clean slate, then create the network the whole cluster shares.
cluster_down
trap cluster_down EXIT INT TERM
docker network create "$NETWORK" >/dev/null

# Start one detached container per slave; collect their in-network URLs.
urls=()
for (( i = 1; i <= SLAVES; i++ )); do
    name="${SLAVE_PREFIX}-$i"
    echo "==> starting slave container $name (inner $INNER_PORT)"
    # --init : tini as PID 1 forwards SIGTERM to thoth (which installs no signal
    # handler, so as PID 1 it would otherwise ignore it) -> `docker rm -f` / stop
    # terminate the slave promptly instead of waiting for the 10s SIGKILL timeout.
    docker run -d --init --name "$name" --network "$NETWORK" "$THOTH_IMAGE" \
        -server "$INNER_PORT" >/dev/null
    urls+=( "http://$name:$INNER_PORT" )
done

# Wait until each slave accepts connections. The runtime image has no curl but
# has bash, so probe its own listening socket from inside the container.
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
# Master in the foreground so it owns this console (live -t progress bar) and so
# Ctrl-C reaches it; on exit the trap removes every container and the network.
# --rm here removes the master itself; slaves are removed by cluster_down.
# --init is essential: without it thoth is the container's PID 1, and since it
# installs no signal handler the kernel ignores the SIGINT the docker CLI forwards
# on Ctrl-C -> `docker run` never returns, the EXIT/INT trap never fires, and
# master+slaves leak. tini (PID 1 via --init) forwards the signal so thoth exits,
# docker run returns, and cluster_down tears the whole cluster down.
docker run --rm -t --init --name "$MASTER" --network "$NETWORK" -p "$PORT:$INNER_PORT" \
    "$THOTH_IMAGE" -cluster "$INNER_PORT" "${urls[@]}"
