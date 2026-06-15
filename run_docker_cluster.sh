#!/usr/bin/env bash
# ---------------------------------------------------------------------------
# run_docker_cluster.sh - Build the Thoth image and run a Dockerised cluster.
#
# Builds the production image (Dockerfile), creates a private Docker network,
# launches N slave containers (ordinary `thoth -server`) on inner ports, and a
# master (`thoth -cluster`) that splits an MCL pricing's paths across them. Only
# the master is published to the host (on --port); the slaves stay on the
# network. Ctrl-C tears the whole cluster (containers + network) down.
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

NET="thoth-cluster-net"
MASTER="thoth-master"
SLAVE_PREFIX="thoth-slave"
SLAVE_PORT_BASE=8090 #!< slave i listens on SLAVE_PORT_BASE + i (inner, not published)

# Tear everything we created down on any exit (success, error, Ctrl-C).
cleanup() {
    echo "==> Tearing down cluster ..."
    docker rm -f "$MASTER" >/dev/null 2>&1 || true
    for (( i = 1; i <= SLAVES; i++ )); do
        docker rm -f "${SLAVE_PREFIX}-${i}" >/dev/null 2>&1 || true
    done
    docker network rm "$NET" >/dev/null 2>&1 || true
}
trap cleanup EXIT INT TERM

thoth_build_image

# Fresh network (remove a stale one from a previous aborted run).
docker network rm "$NET" >/dev/null 2>&1 || true
docker network create "$NET" >/dev/null
echo "==> Network '$NET' created"

# --- launch the slaves (inner ports, network-only) -------------------------
SLAVE_URLS=()
for (( i = 1; i <= SLAVES; i++ )); do
    sport=$(( SLAVE_PORT_BASE + i ))
    name="${SLAVE_PREFIX}-${i}"
    docker rm -f "$name" >/dev/null 2>&1 || true
    echo "==> starting slave $i ($name) on inner port $sport"
    docker run -d --network "$NET" --name "$name" "$THOTH_IMAGE" -server "$sport" >/dev/null
    SLAVE_URLS+=( "http://${name}:${sport}" )
done

# Wait until each slave's port accepts connections (bash /dev/tcp inside the
# container — the runtime image has no curl, but it has bash).
wait_slave() {
    local name="$1" port="$2"
    for _ in $(seq 1 150); do
        if docker exec "$name" bash -c "exec 3<>/dev/tcp/127.0.0.1/${port}" 2>/dev/null; then
            return 0
        fi
        sleep 0.2
    done
    echo "error: slave $name did not become ready on port $port" >&2
    return 1
}
for (( i = 1; i <= SLAVES; i++ )); do
    wait_slave "${SLAVE_PREFIX}-${i}" "$(( SLAVE_PORT_BASE + i ))"
done
echo "==> all $SLAVES slave(s) ready"

# --- launch the master (published on the host port) ------------------------
docker rm -f "$MASTER" >/dev/null 2>&1 || true
echo "==> starting master ($MASTER) on host port $PORT over $SLAVES slave(s)"
docker run -d --network "$NET" --name "$MASTER" -p "${PORT}:8080" \
    "$THOTH_IMAGE" -cluster 8080 "${SLAVE_URLS[@]}" >/dev/null

# Wait for the master's HTTP endpoint (published, so curl from the host).
for _ in $(seq 1 150); do
    if curl -sf "http://localhost:${PORT}/health" >/dev/null 2>&1; then break; fi
    sleep 0.2
done

cat <<EOF
==> Cluster up:  master http://localhost:${PORT}  +  ${SLAVES} slave(s)
    price a book through the master, e.g.:
      curl -X POST -H "Content-Type: application/x-yaml" \\
           --data-binary @samples/heston_call.yaml http://localhost:${PORT}/price
    (Ctrl-C to stop the cluster)
EOF

# Stay in the foreground streaming the master log; Ctrl-C triggers cleanup.
# (no `exec`: keep bash as the parent so the EXIT/INT trap tears the cluster down)
docker logs -f "$MASTER" || true
