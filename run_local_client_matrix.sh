#!/usr/bin/env bash
# ---------------------------------------------------------------------------
# run_local_client_matrix.sh - Submit the pricer matrix to a Thoth server and
#                              print a per-product results table.
#
# POSTs a matrix YAML (default samples/matrix.yaml) to a running Thoth server's
# /price endpoint, then renders one row per priced cell with its method, the time
# the engine spent on it, the premium and every Greek. The server must already be
# running (see run_docker_server.sh or `thoth -server`).
#
# Each pricer in the matrix writes a <product>_<method>_result block; this script
# reads those blocks back, splits the engine suffix (ana / mcl / pde / mcl_pseudo)
# off the product label, and tabulates premium + Greeks (a dash where a Greek was
# not requested for that cell).
#
# Usage:
#     ./run_local_client_matrix.sh [input.yaml] [--port <port>] [--raw <out.yaml>]
#
# The input argument is optional and defaults to samples/matrix.yaml. --raw keeps
# the full YAML response at the given path (otherwise it goes to a temp file).
#
# Examples:
#     ./run_local_client_matrix.sh                         # samples/matrix.yaml, port 8080
#     ./run_local_client_matrix.sh --port 7777             # other port
#     ./run_local_client_matrix.sh samples/matrix.yaml --raw /tmp/matrix.out.yaml
# ---------------------------------------------------------------------------
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$ROOT"

PORT=8080
INPUT=""
RAW=""        #!< optional path to keep the full YAML response

usage() { echo "usage: $0 [input.yaml] [--port <port>] [--raw <out.yaml>]"; }

# Flags may appear anywhere; the first non-flag token is the input YAML.
while [[ $# -gt 0 ]]; do
    case "$1" in
        --port)    PORT="$2"; shift 2 ;;
        --port=*)  PORT="${1#*=}"; shift ;;
        --raw)     RAW="$2"; shift 2 ;;
        --raw=*)   RAW="${1#*=}"; shift ;;
        -h | --help) usage; exit 0 ;;
        -*) echo "error: unknown argument '$1'" >&2; usage >&2; exit 1 ;;
        *)
            if [[ -z "$INPUT" ]]; then INPUT="$1"
            else echo "error: unexpected argument '$1'" >&2; usage >&2; exit 1
            fi
            shift ;;
    esac
done

[[ -z "$INPUT" ]] && INPUT="samples/matrix.yaml"
[[ -f "$INPUT" ]] || { echo "error: input file '$INPUT' not found" >&2; usage >&2; exit 1; }

if [[ -n "$RAW" ]]; then OUT="$RAW"; else OUT="$(mktemp -t thoth_matrix.XXXXXX.yaml)"; trap 'rm -f "$OUT"' EXIT; fi
URL="http://localhost:${PORT}/price"

echo "==> submitting $INPUT -> $URL"
# Content-Type is REQUIRED: without it curl defaults to x-www-form-urlencoded,
# which the server caps at 8 KB (413). -f fails on HTTP errors instead of writing
# the error body into our table input.
curl -fsS -X POST -H "Content-Type: application/x-yaml" --data-binary "@${INPUT}" "$URL" > "$OUT"
[[ -n "$RAW" ]] && echo "==> raw response: $(cd "$(dirname "$OUT")" && pwd)/$(basename "$OUT")"
echo

# Render the table. Each "<name>_result:" block (kind: pricer_result) becomes a row;
# the trailing ana/mcl/pde/mcl_pseudo token is the method, the rest is the product.
# We read the block's top-level scalars (two-space indent) for time/premium/Greeks.
awk '
function flush() {
    if (name == "" || !is_pricer) { name=""; return }
    n = name; sub(/_result$/, "", n)
    if (n ~ /_mcl_pseudo$/)      { method="mcl_pseudo"; prod=substr(n, 1, length(n)-11) }
    else if (n ~ /_(ana|mcl|pde)$/) { method=substr(n, length(n)-2);  prod=substr(n, 1, length(n)-4) }
    else                        { method="-";          prod=n }
    rows[nr] = sprintf("%-40s %-11s %10s %12s %10s %10s %10s %10s %10s",
                       prod, method, fmt(t), fmt(prem), fmt(de), fmt(ga), fmt(ve), fmt(rh), fmt(th))
    nr++
    name=""
}
function fmt(v) { return (v == "") ? "-" : sprintf("%.6g", v+0) }
function reset() { t=prem=de=ga=ve=rh=th=""; is_pricer=0 }
BEGIN {
    nr = 0   #!< numeric from the start: rows[nr] must index 0,1,2... not the string ""
    printf "%-40s %-11s %10s %12s %10s %10s %10s %10s %10s\n",
           "PRODUCT", "METHOD", "TIME(s)", "PREMIUM", "DELTA", "GAMMA", "VEGA", "RHO", "THETA"
    printf "%-40s %-11s %10s %12s %10s %10s %10s %10s %10s\n",
           "-------", "------", "-------", "-------", "-----", "-----", "----", "---", "-----"
}
# a top-level result block header: "<name>_result:" with no leading space
/^[A-Za-z0-9_]+_result:[[:space:]]*$/ {
    flush(); reset()
    name = $1; sub(/:$/, "", name)
    if (name == "matrix_result") name = ""   #!< the !sequence aggregate, not a product
    next
}
# any other top-level key closes the current block
/^[^[:space:]]/ { flush(); reset(); next }
# scalars inside the block (exactly two-space indent, unprefixed top-level fields)
name != "" && /^  kind:[[:space:]]*pricer_result/ { is_pricer=1 }
name != "" && /^  exec_time:/ { t=$2 }
name != "" && /^  premium:/   { prem=$2 }
name != "" && /^  delta:/     { de=$2 }
name != "" && /^  gamma:/     { ga=$2 }
name != "" && /^  vega:/      { ve=$2 }
name != "" && /^  rho:/       { rh=$2 }
name != "" && /^  theta:/     { th=$2 }
END {
    flush()
    for (i = 0; i < nr; i++) print rows[i]
    printf "\n%d priced cell(s).\n", nr
}
' "$OUT"
