#!/usr/bin/env bash
# Build Thoth and price the feature-dense sample with the node-graph dump on.
# The Monte-Carlo node graph of every priced tree comes back inside the result
# YAML as <tree>_mcl_graph fields (premium plus one per Greek) — there is no .dot
# or .png artifact on disk. This script just prices it and lists the fields; copy
# a field's text into Graphviz (e.g. `dot -Tpng`) if you want to render one.
set -euo pipefail
cd "$(dirname "${BASH_SOURCE[0]}")"

cmake -B build
cmake --build build -j

#! input book to price — set it here (or override on the command line:
#! ./build_run.sh <input.yaml>)
INPUT="${1:-samples/big_option.yaml}"
#! output name: insert ".out" before the ".yaml" of the input
OUT="${INPUT%.yaml}.out.yaml"
./build/thoth -batch "$INPUT" "$OUT"

echo
echo "node-graph fields in $OUT:"
grep -oE "[a-zA-Z_]+_mcl_graph:" "$OUT" | sed 's/^/  /'
