#!/usr/bin/env bash
# ---------------------------------------------------------------------------
# run_docker_common.sh - shared helpers for the run_docker_*.sh wrappers.
#
# Sourced (not executed) by run_docker_server.sh / run_docker_batch.sh.
# Mutualises what they share: building the single Thoth production image
# (Dockerfile) once, tagged "thoth" (the git commit is baked in and printed in the
# banner so a stale image is obvious; Docker layer caching makes rebuilds a no-op
# when src/ is unchanged), and the input/output path helpers the batch wrapper uses.
#
# Requires the caller to have set ROOT (the project directory).
# ---------------------------------------------------------------------------

# The single image all three modes (batch / server / cluster) run from.
THOTH_IMAGE="thoth"

# CUDA image tag for the GPU build. NVIDIA only ships a 24.04 cuda base from 12.6
# onward (12.4.x stops at ubuntu22.04); 12.6 is binary-compatible (same
# libcudart.so.12) and targets sm_89.
THOTH_CUDA_TAG="12.6.3"

# Build (or rebuild from cache) the Thoth image. Idempotent. Sets THOTH_IMAGE.
#   thoth_build_image            -> CPU image, tagged "thoth"
#   thoth_build_image --gpu 89   -> GPU (CUDA) image, tagged "thoth-gpu", sm_<arch>
thoth_build_image() {
    command -v docker >/dev/null 2>&1 || { echo "error: docker not found" >&2; return 1; }
    local commit gpu_arch=""
    [[ "${1:-}" == "--gpu" ]] && gpu_arch="${2:-89}"
    commit="$(git -C "$ROOT" rev-parse --short HEAD 2>/dev/null || echo unknown)"
    if [[ -n "$gpu_arch" ]]; then
        # GPU variant of the single Dockerfile: nvidia/cuda base images + CUDA on.
        THOTH_IMAGE="thoth-gpu"
        echo "==> Building GPU image '$THOTH_IMAGE' (CUDA $THOTH_CUDA_TAG, sm_$gpu_arch) at commit $commit ..." >&2
        docker build \
            --build-arg BUILD_BASE="nvidia/cuda:${THOTH_CUDA_TAG}-devel-ubuntu24.04" \
            --build-arg RUNTIME_BASE="nvidia/cuda:${THOTH_CUDA_TAG}-runtime-ubuntu24.04" \
            --build-arg ENABLE_CUDA=ON --build-arg CUDA_ARCH="$gpu_arch" \
            --build-arg GIT_COMMIT="$commit" -t "$THOTH_IMAGE" "$ROOT"
    else
        THOTH_IMAGE="thoth"
        echo "==> Building image '$THOTH_IMAGE' at commit $commit ..." >&2
        docker build --build-arg GIT_COMMIT="$commit" -t "$THOTH_IMAGE" "$ROOT"
    fi
}

# Validate an input YAML path and echo its absolute path (exit non-zero on error).
# Shared by the batch and client wrappers, which both take a positional input.
require_input_file() {
    local in="${1:-}"
    [[ -n "$in" ]] || { echo "error: an input YAML file is required" >&2; return 1; }
    [[ -f "$in" ]] || { echo "error: input file '$in' not found" >&2; return 1; }
    realpath "$in"
}

# Default result path for an input: <input>.out.yaml (replace the .yaml suffix, or
# append if there is none). Shared by the batch and client wrappers.
default_output() {
    local in="$1"
    if [[ "$in" == *.yaml ]]; then echo "${in%.yaml}.out.yaml"; else echo "${in}.out.yaml"; fi
}
