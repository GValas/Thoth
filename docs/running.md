# Running Thoth

How to build the `thoth` binary, drive its four run modes, and use the Docker
wrapper scripts. Everything here is verified against `src/thoth.cpp`, the
`Dockerfile` and the `run_*.sh` scripts; see [`README.md`](../README.md) for the
configuration / YAML reference.

## Build locally

Dependencies (Debian / Ubuntu): see the *Build* section of the README
(`build-essential cmake pkg-config libboost-all-dev libyaml-cpp-dev
libcpp-httplib-dev`). Then:

```bash
cmake -B build
cmake --build build -j          # -> ./build/thoth (+ ./build/thoth_tests)
```

The build produces the `thoth` binary and the doctest suite. Run the tests with:

```bash
ctest --test-dir build --output-on-failure
```

Running `./build/thoth` with no arguments (or with bad arguments) prints a help
banner describing the four modes below and exits.

## The four run modes

The binary's mode is selected by the first argument. Argument counts are
validated in `GetRunningMode`; an invalid invocation falls back to the help
banner and a non-zero exit.

### `-batch <input.yaml> <output.yaml> [exec_name]`

Read the task in `input.yaml`, run it, and write `output.yaml` (the input config
with the computed `*_result` blocks added). The optional `exec_name` overrides
which object to execute; it defaults to the `root` object.

```bash
./build/thoth -batch samples/simple_call.yaml /tmp/simple_call.out.yaml
./build/thoth -batch samples/matrix.yaml /tmp/matrix.out.yaml
```

### `-server <port>`

Start the HTTP pricing service, listening on `0.0.0.0:<port>`:

- `POST /price` — YAML body in, YAML result out. An optional `X-Exec-Name`
  header selects the object to run (default: the `root` object).
- `GET /health` — returns `ok`.
- `GET /progress` — `"<current> <total> <active>"` of the in-flight pricing
  (this is what the cluster master polls).

The port must be in `1..65535` or the binary exits with an error.

```bash
./build/thoth -server 8080
# in another shell:
curl --data-binary @samples/simple_call.yaml localhost:8080/price
```

### `-client <url> <input.yaml>`

POST `input.yaml` to a running `thoth` server and print the result to stdout.

```bash
./build/thoth -client http://localhost:8080 samples/heston_call.yaml
```

### `-cluster <port> <slave-url> [slave-url ...]`

Run a master server on `<port>` that splits a single-MCL-pricer book's `paths`
across the listed slave `-server` instances, dispatches the sub-requests over
HTTP and aggregates the pooled premium / trust / Greeks. Any other root (a
non-MCL engine, a `!sequence`, ...) is computed by the master itself. The master
exposes `POST /price` and `GET /health`.

```bash
./build/thoth -server 8091 &                     # slaves
./build/thoth -server 8092 &
./build/thoth -cluster 8090 http://localhost:8091 http://localhost:8092
# then post a book to the master:
./build/thoth -client http://localhost:8090 samples/simple_call.yaml
```

## Docker wrappers

Each wrapper builds the production image from the `Dockerfile` (idempotent; the
git commit is baked in and printed in the banner) and then runs it. All three
print a `usage:` line on bad arguments. Default output paths are
`<input>.out.yaml`, which are gitignored.

### `run_docker_server.sh [--gpu] [--slaves <n>] [--port <port>] [--arch <cc>]`

Build and serve the HTTP pricing service, publishing it on the host `--port`
(default `8080`; the container always listens on `8080` inside).

- default (no `--slaves`, or `--slaves 0`): a single server container.
- `--slaves N` (N >= 1): a cluster of N detached slave containers plus a
  foreground master, all on a private Docker network. Only the master's port is
  published. Ctrl-C (or the master exiting) tears the whole cluster — master,
  slaves and network — down.
- `--gpu`: build/run the CUDA (`mcl_gpu`) variant on an NVIDIA GPU (needs the
  NVIDIA Container Toolkit), attaching `--gpus all` to every container.
- `--arch <cc>`: CUDA compute capability for the `--gpu` build (default `89`,
  Ada / RTX 40-series).

```bash
./run_docker_server.sh                       # single CPU server, host port 8080
./run_docker_server.sh --port 7777           # single CPU server, host port 7777
./run_docker_server.sh --slaves 4            # cluster: 4 slaves + master on 8080
./run_docker_server.sh --gpu --arch 86       # single GPU server, sm_86 (Ampere)
```

### `run_docker_batch.sh <input.yaml> [output.yaml]`

Build and price one YAML file in batch inside a container. The output defaults to
`<input>.out.yaml` next to the input. The input directory is bind-mounted
read-only, the output directory read-write, and `--user` maps the container to
the invoking host user so the result is owned by you (not root).

```bash
./run_docker_batch.sh samples/heston_call.yaml          # -> samples/heston_call.out.yaml
./run_docker_batch.sh samples/sabr_call.yaml /tmp/sabr.yaml
```

### `run_local_client.sh <input.yaml> [output.yaml] [--port <port>] [--exec-name <name>]`

POST one YAML file to an already-running Thoth server's `/price` and write the
response (default `<input>.out.yaml`). `--port` defaults to `8080`;
`--exec-name` sets the `X-Exec-Name` header. The wrapper sends
`Content-Type: application/x-yaml`, which is required for bodies over ~8 KB (the
default form content type is capped by the HTTP library at 8 KB).

```bash
# with a server (or cluster) already up on 8080:
./run_local_client.sh samples/simple_call.yaml              # -> samples/simple_call.out.yaml
./run_local_client.sh samples/matrix.yaml /tmp/m.yaml --port 7777
./run_local_client.sh samples/heston_call.yaml --exec-name heston_pricing
```

## End-to-end examples

```bash
# 1) Local batch: price the 1y ATM call (Black-Scholes ~15.71)
cmake -B build && cmake --build build -j
./build/thoth -batch samples/simple_call.yaml /tmp/out.yaml

# 2) Dockerised server + client
./run_docker_server.sh --port 8080 &                  # leave it running
./run_local_client.sh samples/heston_call.yaml --port 8080

# 3) Dockerised cluster: split an MCL book across 2 slaves
./run_docker_server.sh --port 8090 --slaves 2 &
./run_local_client.sh samples/simple_call.yaml --port 8090

# 4) Run the full pricer/product matrix in one process
./run_docker_batch.sh samples/matrix.yaml             # -> samples/matrix.out.yaml
```

## Notes

- **Request serialisation.** The server prices one request at a time (global
  engine state). It logs the client IP on arrival, and again if a request has to
  wait for the lock. The `/progress` and `/health` endpoints stay responsive
  while a price runs.
- **Live progress bar.** The Docker wrappers pass `docker run -t`, so the
  container console shows the live single-line `│███░░░│` progress bar. When
  stdout is not a TTY (e.g. captured later via `docker logs`), the bar degrades
  to one line every 10% so logs stay readable.
- **Ctrl-C teardown.** Containers run with `--init` (tini as PID 1) so Ctrl-C is
  forwarded and the container stops cleanly. For a cluster, the master's exit
  trap removes the master, every slave and the private network. If a `-server`
  client disconnects mid-pricing, the run is cancelled instead of finishing a
  result nobody will read.

## See also

[gpu.md](gpu.md) · [monte_carlo.md](monte_carlo.md) · [pde.md](pde.md) · [products.md](products.md) · [volatility.md](volatility.md) · [agent.md](agent.md)
