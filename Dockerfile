# ---------------------------------------------------------------------------
# Thoth - production image (multi-stage, CPU or GPU from one file)
#
# A single parameterised Dockerfile builds both the CPU image and the CUDA
# (mcl_gpu) image: the only differences are the base images and one cmake flag,
# all driven by build-args. The wrapper scripts pass the right values, so you
# normally never type these by hand.
#
#   CPU (default):
#     docker build -t thoth .
#     docker run --rm -p 8080:8080 thoth                # HTTP pricing server
#     curl --data-binary @input.yaml localhost:8080/price
#     # no -t needed: in server mode pricing progress is logged as one line
#     # every 10%, so `docker logs` stays clean.
#     docker run --rm -v "$PWD/samples:/data" thoth \
#         -batch /data/input.yaml /data/output.yaml /data/log.txt   # batch
#
#   GPU (CUDA mcl_gpu engine, needs an NVIDIA GPU + the NVIDIA Container Toolkit):
#     docker build -t thoth-gpu \
#         --build-arg BUILD_BASE=nvidia/cuda:12.6.3-devel-ubuntu24.04 \
#         --build-arg RUNTIME_BASE=nvidia/cuda:12.6.3-runtime-ubuntu24.04 \
#         --build-arg ENABLE_CUDA=ON --build-arg CUDA_ARCH=89 .
#     docker run --rm --gpus all -p 8080:8080 thoth-gpu             # GPU HTTP server
#     # Prefer the wrapper: ./run_docker_server.sh --gpu
#
# Books that are not GPU-supported (or use any other method) still price on the
# CPU even in the GPU image.
# ---------------------------------------------------------------------------

# Base images (overridden to the nvidia/cuda images for the GPU build). The CUDA
# images are themselves ubuntu24.04-based, so the apt package names below are the
# same in both builds. NVIDIA only ships a 24.04 cuda base from 12.6 onward
# (12.4.x stops at ubuntu22.04); 12.6 is binary-compatible (same libcudart.so.12)
# and targets sm_89.
ARG BUILD_BASE=ubuntu:24.04
ARG RUNTIME_BASE=ubuntu:24.04

# ---- build stage : full toolchain + -dev packages (discarded) -------------
FROM ${BUILD_BASE} AS build

RUN apt-get update && export DEBIAN_FRONTEND=noninteractive \
    && apt-get install -y --no-install-recommends \
        build-essential \
        cmake \
        clang-format \
        pkg-config \
        libgsl-dev \
        libboost-all-dev \
        libyaml-cpp-dev \
        libcpp-httplib-dev \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /src
COPY CMakeLists.txt ./
COPY src ./src
COPY samples ./samples
# git commit baked into the binary (printed at startup); the wrappers pass it.
# ENABLE_CUDA=ON + CUDA_ARCH select the GPU build; on a non-CUDA base ENABLE_CUDA
# stays OFF and the architectures flag is harmlessly ignored (CUDA language off).
# CUDA_ARCH is the compute capability of the target GPU (89 = Ada / RTX 40-series).
ARG GIT_COMMIT=unknown
ARG ENABLE_CUDA=OFF
ARG CUDA_ARCH=89
RUN cmake -B build -DCMAKE_BUILD_TYPE=Release -DTHOTH_BUILD_TESTS=OFF \
        -DTHOTH_BUILD_ID="${GIT_COMMIT}" \
        -DTHOTH_ENABLE_CUDA="${ENABLE_CUDA}" -DCMAKE_CUDA_ARCHITECTURES="${CUDA_ARCH}" \
    && cmake --build build -j

# ---- runtime stage : only the shared libraries the binary links -----------
# For the GPU build, RUNTIME_BASE is a nvidia/cuda *runtime* image: it carries
# libcudart.so.12 (the one NVIDIA lib the binary links — cuRAND is used through
# its header-only device API, so no host libcurand is needed).
FROM ${RUNTIME_BASE} AS runtime

# Runtime libs (Boost is header-only here, so it is intentionally absent):
#   libgsl27 libgslcblas0  (GSL)   libyaml-cpp0.8 (YAML)  libcpp-httplib0.14t64 (HTTP)
RUN apt-get update && export DEBIAN_FRONTEND=noninteractive \
    && apt-get install -y --no-install-recommends \
        libgsl27 \
        libgslcblas0 \
        libyaml-cpp0.8 \
        libcpp-httplib0.14t64 \
    && apt-get clean && rm -rf /var/lib/apt/lists/*

COPY --from=build /src/build/thoth /usr/local/bin/thoth
COPY --from=build /src/samples /opt/thoth/samples

WORKDIR /opt/thoth
EXPOSE 8080

# default: HTTP pricing service on 8080 (override args for batch/client)
ENTRYPOINT ["thoth"]
CMD ["-server", "8080"]
