#!/usr/bin/env bash
# Build Thoth, price the feature-dense sample, and render its MC node graph.
# The graph now comes back inside the result YAML (premium_mcl_graph), so we
# extract that .dot text and render it with Graphviz.
set -euo pipefail
cd "$(dirname "${BASH_SOURCE[0]}")"

cmake -B build
cmake --build build -j

OUT=samples/big_option.out.yaml
DOT=samples/big_option_nodes.dot
PNG=samples/big_option_nodes.png

./build/thoth -batch samples/big_option.yaml "$OUT"

# extract the premium_mcl_graph .dot (a double-quoted, JSON-escaped YAML scalar)
python3 - "$OUT" "$DOT" <<'PY'
import re, json, sys
y = open(sys.argv[1]).read()
m = re.search(r'premium_mcl_graph:\s*("(?:[^"\\]|\\.)*")', y)
if not m:
    sys.exit("premium_mcl_graph not found (is debug_configuration.generate_nodes_graph on?)")
open(sys.argv[2], "w").write(json.loads(m.group(1)))
PY
echo "dot: $DOT"

if command -v dot >/dev/null 2>&1; then
    dot -Tpng "$DOT" -o "$PNG"
    echo "png: $PNG"
else
    echo "(install graphviz to render: dot -Tpng $DOT -o $PNG)"
fi
