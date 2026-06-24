#!/usr/bin/env bash
# Build Thoth and price a book with the node-graph dump on. The Monte-Carlo node
# graph of every priced tree comes back inside the result YAML as <tree>_mcl_graph
# fields (premium plus one per Greek) — no .dot or .png artifact on disk; copy a
# field's text into Graphviz (e.g. `dot -Tpng`) to render one.
#
# Usage: ./build_run.sh [--gpu] [input.yaml]
#   --gpu   build the CUDA GPU Monte-Carlo engine (-DTHOTH_ENABLE_CUDA=ON; needs
#           nvcc + an NVIDIA GPU). Without it, the CPU-only build is used.
set -euo pipefail
# this script lives in scripts/; run from the project root (one level up)
cd "$(dirname "${BASH_SOURCE[0]}")/.."

#! --gpu -> configure with CUDA enabled; the remaining argument is the input book
CMAKE_GPU=()
INPUT=""
for a in "$@"; do
    case "$a" in
        --gpu) CMAKE_GPU=(-DTHOTH_ENABLE_CUDA=ON) ;;
        *) INPUT="$a" ;;
    esac
done
INPUT="${INPUT:-samples/big_option.yaml}"

cmake -B build "${CMAKE_GPU[@]}"
cmake --build build -j

#! output name: insert ".out" before the ".yaml" of the input
OUT="${INPUT%.yaml}.out.yaml"
./build/thoth -batch "$INPUT" "$OUT"

#! list any node-graph fields (only present when debug_configuration.generate_nodes_graph
#! is on); tolerate none without failing the script under `set -euo pipefail`
fields="$(grep -oE "[a-zA-Z_]+_mcl_graph:" "$OUT" || true)"
echo
if [ -n "$fields" ]; then
    echo "node-graph fields in $OUT:"
    echo "$fields" | sed 's/^/  /'
else
    echo "(no node-graph fields in $OUT — set debug_configuration.generate_nodes_graph: true to emit them)"
fi
