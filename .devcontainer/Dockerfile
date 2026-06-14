FROM mcr.microsoft.com/devcontainers/base:ubuntu-24.04

# Build toolchain + Thoth dependencies
#   GSL            : libgsl-dev
#   Boost          : libboost-all-dev (only date_time is linked; header-only at runtime)
#   YAML           : libyaml-cpp-dev          (config format)
#   HTTP server    : libcpp-httplib-dev       (HTTP pricing service)
RUN apt-get update && export DEBIAN_FRONTEND=noninteractive \
    && apt-get install -y --no-install-recommends \
        build-essential \
        g++ \
        make \
        cmake \
        pkg-config \
        gdb \
        git \
        libgsl-dev \
        libboost-all-dev \
        libyaml-cpp-dev \
        libcpp-httplib-dev \
    && apt-get clean && rm -rf /var/lib/apt/lists/*
