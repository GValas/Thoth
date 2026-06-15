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
txt = open(sys.argv[1]).read()
# (product, method, result-object) — mono/multi underlying; sorted alphabetically
cells = [
    ("barrier mono continuous", "ana", "p_barr_cont_ana"),
    ("barrier mono continuous", "mcl", "p_barr_cont_mcl"),
    ("barrier mono continuous", "pde", "p_barr_cont_pde"),
    ("barrier mono discrete",   "mcl", "p_barr_disc_mcl"),
    ("barrier mono discrete",   "pde", "p_barr_disc_pde"),
    ("vanilla mono am put",     "mcl", "p_van_am_mcl"),
    ("vanilla mono am put",     "pde", "p_van_am_pde"),
    ("vanilla mono am put qto", "mcl", "p_van_am_q_mcl"),
    ("vanilla mono am put qto", "pde", "p_van_am_q_pde"),
    ("vanilla mono eu",         "ana", "p_van_eu_ana"),
    ("vanilla mono eu",         "mcl", "p_van_eu_mcl"),
    ("vanilla mono eu",         "pde", "p_van_eu_pde"),
    ("vanilla mono eu bates",   "ana", "p_bates_ana"),
    ("vanilla mono eu bates",   "mcl", "p_bates_mcl"),
    ("vanilla mono eu compo",   "ana", "p_compo_ana"),
    ("vanilla mono eu compo",   "mcl", "p_compo_mcl"),
    ("vanilla mono eu compo",   "pde", "p_compo_pde"),
    ("vanilla mono eu heston",  "ana", "p_heston_ana"),
    ("vanilla mono eu heston",  "mcl", "p_heston_mcl"),
    ("vanilla mono eu heston",  "pde", "p_heston_pde"),
    ("vanilla mono eu quanto",  "ana", "p_van_eu_q_ana"),
    ("vanilla mono eu quanto",  "mcl", "p_van_eu_q_mcl"),
    ("vanilla mono eu quanto",  "pde", "p_van_eu_q_pde"),
    ("vanilla mono us compo",   "mcl", "p_compo_am_mcl"),
    ("vanilla mono us compo",   "pde", "p_compo_am_pde"),
    ("vanilla multi eu basket", "ana", "p_basket_ana"),
    ("vanilla multi eu basket", "mcl", "p_basket_mcl"),
    ("vanilla multi eu basket", "pde", "p_basket_pde"),
    ("vanilla multi eu best-of","mcl", "p_rb_best_mcl"),
    ("vanilla multi eu worst-of","mcl", "p_rb_worst_mcl"),
    ("variance swap mono", "ana", "p_vswap_ana"),
    ("variance swap mono", "mcl", "p_vswap_mcl"),
]
greeks = ["delta", "gamma", "vega", "rho", "theta"]
def field(block, name):
    m = re.search(rf'\b{name}: ([\-0-9.eE]+)', block)
    return float(m.group(1)) if m else None
hdr = f"    {'case':27s}{'method':>7s}{'time(s)':>10s}{'premium':>13s}{'trust':>10s}"
hdr += "".join(f"{g:>11s}" for g in greeks)
print(hdr)
print("    " + "-" * (len(hdr) - 4))
for product, method, obj in cells:
    m = re.search(rf'^{obj}_result:\n((?: .*\n)+)', txt, re.M)
    blk = m.group(1) if m else ""
    p = field(blk, "premium")
    if p is None:
        print(f"    {product:27s}{method:>7s}{'(missing)':>23s}")
        continue
    secs = field(blk, "exec_time")
    t = field(blk, "premium_trust")
    row = f"    {product:27s}{method:>7s}{(secs or 0):10.3f}{p:13.5f}{(t or 0):10.4f}"
    for g in greeks:
        v = field(blk, g)
        row += f"{v:11.5f}" if v is not None else f"{'-':>11s}"
    print(row)
PY