#!/usr/bin/env bash
# ---------------------------------------------------------------------------
# run_docker_common.sh - shared helpers for the run_docker_*.sh wrappers.
#
# Sourced (not executed) by run_docker_batch.sh / run_docker_server.sh /
# run_docker_cluster.sh. Mutualises the one thing they all need: building the
# single Thoth production image (Dockerfile) once, tagged "thoth". The git commit
# is baked in (printed in the startup banner) so a stale image is obvious; Docker
# layer caching makes rebuilds a no-op when src/ is unchanged.
#
# Requires the caller to have set ROOT (the project directory).
# ---------------------------------------------------------------------------

# The single image all three modes (batch / server / cluster) run from.
THOTH_IMAGE="thoth"

# Build (or rebuild from cache) the Thoth image. Idempotent.
thoth_build_image() {
    command -v docker >/dev/null 2>&1 || { echo "error: docker not found" >&2; return 1; }
    local commit
    commit="$(git -C "$ROOT" rev-parse --short HEAD 2>/dev/null || echo unknown)"
    echo "==> Building image '$THOTH_IMAGE' at commit $commit ..." >&2
    docker build --build-arg GIT_COMMIT="$commit" -t "$THOTH_IMAGE" "$ROOT"
}
