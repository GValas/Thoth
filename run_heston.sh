#!/usr/bin/env bash
# ---------------------------------------------------------------------------
# run_heston.sh - Price the Heston stochastic-vol sample through a running server.
#
# Posts samples/heston_call.yaml (a 1y ATM call priced by all three engines on a
# Heston underlying) to a running thoth server, writes the result next to the
# input as <input>.out.yaml, then prints the ANA / PDE / MCL premiums + Greeks as
# a table — they should agree (ANA characteristic function, PDE 2-D ADI, MCL QE).
#
# The server must already be running (see run_serve.sh, or: thoth -server 8080).
#
# Usage:
#     ./run_heston.sh                         # samples/heston_call.yaml, port 8080
#     ./run_heston.sh <input.yaml>            # custom input
#     ./run_heston.sh <input.yaml> <port>     # custom input + port
# ---------------------------------------------------------------------------
set -euo pipefail

# Resolve the project directory (location of this script) so it works from anywhere.
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$ROOT"

INPUT="${1:-samples/heston_call.yaml}"
OUTPUT="${INPUT%.yaml}.out.yaml"
PORT="${2:-8080}"
URL="http://localhost:${PORT}/price"

if [[ ! -f "$INPUT" ]]; then
    echo "error: input file '$INPUT' not found" >&2
    exit 1
fi

echo "==> POST $INPUT -> $URL  (output: $OUTPUT)"

# Content-Type is REQUIRED: without it curl sends form-urlencoded, which the
# server caps at 8 KB (413 for larger bodies).
curl -sS -X POST -H "Content-Type: application/x-yaml" \
     --data-binary "@${INPUT}" "$URL" > "$OUTPUT"

ABS_OUTPUT="$(cd "$(dirname "$OUTPUT")" && pwd)/$(basename "$OUTPUT")"
echo "==> Done. Result written to $ABS_OUTPUT ($(wc -c < "$OUTPUT") bytes)"

# Pretty-print the per-engine results (premium / trust / Greeks).
echo
echo "==> Heston results (one model, three engines):"
python3 - "$OUTPUT" <<'PY'
import re, sys
txt = open(sys.argv[1]).read()
rows = [("ANA", "heston_ana_result"), ("PDE", "heston_pde_result"), ("MCL", "heston_mcl_result")]
greeks = ["delta", "gamma", "vega", "rho", "theta"]
def field(block, name):
    m = re.search(rf'\b{name}: ([\-0-9.eE]+)', block)
    return float(m.group(1)) if m else None
hdr = f"    {'engine':7s}{'premium':>13s}{'trust':>10s}" + "".join(f"{g:>11s}" for g in greeks)
print(hdr)
print("    " + "-" * (len(hdr) - 4))
for label, obj in rows:
    m = re.search(rf'^{obj}:\n((?: .*\n)+)', txt, re.M)
    blk = m.group(1) if m else ""
    p = field(blk, "premium")
    if p is None:
        print(f"    {label:7s}{'(missing)':>13s}")
        continue
    row = f"    {label:7s}{p:13.5f}{(field(blk,'premium_trust') or 0):10.4f}"
    for g in greeks:
        v = field(blk, g)
        row += f"{v:11.5f}" if v is not None else f"{'-':>11s}"
    print(row)
PY
