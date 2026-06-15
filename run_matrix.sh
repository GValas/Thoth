#!/usr/bin/env bash
# ---------------------------------------------------------------------------
# run_matrix.sh - Price the full pricer/product matrix through a running server.
#
# Like run_simple_call.sh, but defaults to samples/matrix.yaml — whose
# !sequence root launches every priceable (product, method) combination one
# after another. POSTs it to a running thoth server, writes the result next to
# the input as <input>.out.yaml, then prints the premiums as a table.
#
# The server must already be running (see run_serve.sh, or: thoth -server 8080).
#
# Usage:
#     ./run_matrix.sh                         # samples/matrix.yaml, port 8080
#     ./run_matrix.sh <input.yaml>            # custom input
#     ./run_matrix.sh <input.yaml> <port>     # custom input + port
# ---------------------------------------------------------------------------
set -euo pipefail

# Resolve the project directory (location of this script) so it works from anywhere.
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$ROOT"

INPUT="${1:-samples/matrix.yaml}"
# Default output sits next to the input with a ".out" tag before the extension
# (e.g. matrix.yaml -> matrix.out.yaml). These are gitignored.
OUTPUT="${INPUT%.yaml}.out.yaml"
PORT="${2:-8080}"
URL="http://localhost:${PORT}/price"

if [[ ! -f "$INPUT" ]]; then
    echo "error: input file '$INPUT' not found" >&2
    exit 1
fi

echo "==> POST $INPUT -> $URL  (output: $OUTPUT)"

# The Content-Type header is REQUIRED: without it curl defaults to
# 'application/x-www-form-urlencoded', which makes cpp-httplib apply an 8 KB
# form-payload cap and answer 413 for larger requests.
curl -sS -X POST -H "Content-Type: application/x-yaml" \
     --data-binary "@${INPUT}" "$URL" > "$OUTPUT"

ABS_OUTPUT="$(cd "$(dirname "$OUTPUT")" && pwd)/$(basename "$OUTPUT")"
echo "==> Done. Result written to $ABS_OUTPUT ($(wc -c < "$OUTPUT") bytes)"

# Pretty-print the matrix results as a table (premium + all Greeks).
echo
echo "==> Matrix results (premium / trust / Greeks):"
python3 - "$OUTPUT" <<'PY'
import re, sys
# The script only CROSS-REFERENCES the data: every pricer result block is
# discovered from the output, the row name is the entity name itself (its
# trailing method token split into the 'method' column), and rows are sorted
# alphabetically. Nothing about the products is hard-coded here — the full
# product names live in matrix.yaml as the pricer entity names.
txt = open(sys.argv[1]).read()
METHODS = {"ana", "mcl", "pde"}
greeks = ["delta", "gamma", "vega", "rho", "theta"]
def field(block, name):
    m = re.search(rf'\b{name}: ([\-0-9.eE]+)', block)
    return float(m.group(1)) if m else None

rows = []
for m in re.finditer(r'^(\w+)_result:\n((?: .*\n)+)', txt, re.M):
    name, blk = m.group(1), m.group(2)
    if field(blk, "premium") is None:
        continue                       # skip the sequence / system_information blocks
    parts = name.split("_")
    if parts[-1] not in METHODS:
        continue                       # only (product, method) pricer results
    rows.append((" ".join(parts[:-1]), parts[-1], blk))  # (product, method, block)
rows.sort(key=lambda r: (r[0], r[1]))

hdr = f"    {'product':27s}{'method':>7s}{'time(s)':>10s}{'premium':>13s}{'trust':>10s}"
hdr += "".join(f"{g:>11s}" for g in greeks)
print(hdr)
print("    " + "-" * (len(hdr) - 4))
for product, method, blk in rows:
    secs = field(blk, "exec_time")
    p = field(blk, "premium")
    t = field(blk, "premium_trust")
    row = f"    {product:27s}{method:>7s}{(secs or 0):10.3f}{p:13.5f}{(t or 0):10.4f}"
    for g in greeks:
        v = field(blk, g)
        row += f"{v:11.5f}" if v is not None else f"{'-':>11s}"
    print(row)
PY