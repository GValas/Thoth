FROM mcr.microsoft.com/devcontainers/base:ubuntu-24.04

# Build toolchain + Thoth dependencies
#   Boost          : libboost-all-dev (date_time linked; Boost.Math header-only)
#   YAML           : libyaml-cpp-dev          (config format)
#   HTTP server    : libcpp-httplib-dev       (HTTP pricing service)
RUN apt-get update && export DEBIAN_FRONTEND=noninteractive \
    && apt-get install -y --no-install-recommends \
        build-essential \
        g++ \
        make \
        cmake \
        clang-format \
        pkg-config \
        gdb \
        git \
        libboost-all-dev \
        libyaml-cpp-dev \
        libcpp-httplib-dev \
    && apt-get clean && rm -rf /var/lib/apt/lists/*

# CUDA — minimal toolkit (nvcc) for the GPU Monte-Carlo engine (mcl_gpu).
# Only what the .cu needs to compile: the compiler, the CUDA runtime dev headers
# (cuda_runtime.h) and cuRAND dev headers (device-side curand_kernel.h). We avoid
# the full cuda-toolkit meta-package on purpose: it pulls nsight-systems, which
# depends on libtinfo5 — unavailable on Ubuntu 24.04 — and breaks the build.
# Running the kernel needs the host GPU, attached at runtime via
# devcontainer.json "hostRequirements.gpu" (NVIDIA Container Toolkit on the host).
# NOTE: we use NVIDIA's ubuntu2004 repo on purpose — it carries the CUDA 12.4
# (-12-4) packages; the ubuntu2404 repo only ships 12.5+. The keyring deb is
# repo-specific, so it must come from the same ubuntu2004 path. These are just
# headers + static libs + nvcc, which install cleanly on the 24.04 base.
RUN export DEBIAN_FRONTEND=noninteractive \
    && curl -fsSL https://developer.download.nvidia.com/compute/cuda/repos/ubuntu2004/x86_64/cuda-keyring_1.1-1_all.deb -o /tmp/cuda-keyring.deb \
    && apt-get install -y /tmp/cuda-keyring.deb \
    && apt-get update \
    && apt-get install -y --no-install-recommends \
        cuda-nvcc-12-4 \
        cuda-cudart-dev-12-4 \
        libcurand-dev-12-4 \
    && rm /tmp/cuda-keyring.deb \
    && apt-get clean && rm -rf /var/lib/apt/lists/*

# Put nvcc on PATH (the feature used to do this for us).
ENV PATH=/usr/local/cuda-12.4/bin:${PATH}
