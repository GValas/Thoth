# ---------------------------------------------------------------------------
# Thoth - production image (multi-stage)
#
#   build:  docker build -t thoth .
#   run  :  docker run --rm -p 8080:8080 thoth           # HTTP pricing server
#           curl --data-binary @input.yaml localhost:8080/price
#           # no -t needed: in server mode pricing progress is logged as one line
#           # every 10%, so `docker logs` stays clean.
#   batch:  docker run --rm -v "$PWD/samples:/data" thoth \
#               -batch /data/input.yaml /data/output.yaml /data/log.txt
# ---------------------------------------------------------------------------

# ---- build stage : full toolchain + -dev packages (discarded) -------------
FROM ubuntu:24.04 AS build

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
# git commit baked into the binary (printed at startup); run_serve.sh passes it
ARG GIT_COMMIT=unknown
RUN cmake -B build -DCMAKE_BUILD_TYPE=Release -DTHOTH_BUILD_TESTS=OFF -DTHOTH_BUILD_ID="${GIT_COMMIT}" \
    && cmake --build build -j

# ---- runtime stage : only the shared libraries the binary links -----------
FROM ubuntu:24.04 AS runtime

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
