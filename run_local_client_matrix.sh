#!/usr/bin/env bash
# ---------------------------------------------------------------------------
# run_local_client_matrix.sh - Submit the pricer matrix to a Thoth server and
#                              print a per-product results table.
#
# POSTs a matrix-style YAML (a !sequence of pricers) to a running Thoth server's
# /price endpoint, then renders one row per priced cell with its method, the time
# the engine spent on it, the premium and every Greek. The server must already be
# running (see run_docker_server.sh or `thoth -server`).
#
# Each pricer writes a <product>_<method>_result block; this script reads those
# blocks back, splits the engine suffix (ana / mcl / pde / mcl_pseudo / mcl_gpu)
# off the product label, and tabulates premium + Greeks (a dash where a Greek was
# not requested for that cell). The input path is taken relative to the caller's
# working directory.
#
# Usage:
#     ./run_local_client_matrix.sh <input.yaml> [--port <port>] [--raw <out.yaml>]
#
# <input.yaml> is required. --raw keeps the full YAML response at the given path
# (otherwise it goes to a temp file).
#
# Examples:
#     ./run_local_client_matrix.sh samples/matrix.yaml
#     ./run_local_client_matrix.sh samples/matrix.yaml --port 7777
#     ./run_local_client_matrix.sh samples/matrix.yaml --raw /tmp/matrix.out.yaml
# ---------------------------------------------------------------------------
set -euo pipefail

PORT=8080
INPUT=""
RAW=""        #!< optional path to keep the full YAML response

usage() { echo "usage: $0 <input.yaml> [--port <port>] [--raw <out.yaml>]"; }

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

[[ -z "$INPUT" ]] && { echo "error: an input YAML file is required" >&2; usage >&2; exit 1; }
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
    if (n ~ /_mcl_pseudo$/)         { method="mcl_pseudo"; prod=substr(n, 1, length(n)-11) }
    else if (n ~ /_mcl_gpu$/)       { method="mcl_gpu";    prod=substr(n, 1, length(n)-8) }
    else if (n ~ /_(ana|mcl|pde)$/) { method=substr(n, length(n)-2);  prod=substr(n, 1, length(n)-4) }
    else                            { method="-";          prod=n }
    row = sprintf("%-40s %-11s %10s %12s %10s %10s %10s %10s %10s",
                  prod, toupper(method), fmt_time(t), fmt(prem), fmt(de), fmt(ga), fmt(ve), fmt(rh), fmt(th))
    if (pg != "") row = row "   " pg   #!< model-parameter Greeks (vega_<param>), variable per model
    rows[nr] = row
    #! sort key: product then method. SUBSEP (\034) sorts below '_' and letters, so
    #! a plain product groups before its longer "<product>_<variant>" siblings.
    keys[nr] = prod SUBSEP method
    nr++
    name=""
}
function fmt(v) { return (v == "") ? "-" : sprintf("%.6g", v+0) }
# ~3 significant figures, fixed-point (no scientific), for a unit-scaled value
function tnum(x,   a) {
    a = (x < 0 ? -x : x)
    if (a >= 100) return sprintf("%.0f", x)
    if (a >= 10)  return sprintf("%.1f", x)
    return sprintf("%.2f", x)
}
# a duration in seconds -> human-readable (6.0µs, 5.0ms, 12.2s, 1m2s)
function fmt_time(v,   a, m, s) {
    if (v == "") return "-"
    v = v + 0
    a = (v < 0 ? -v : v)
    if (a == 0)    return "0s"
    if (a >= 60)   { m = int(v / 60); s = v - m * 60
                     return (s >= 0.5) ? sprintf("%dm%ds", m, int(s + 0.5)) : sprintf("%dm", m) }
    if (a >= 1)    return tnum(v) "s"
    if (a >= 1e-3) return tnum(v * 1e3) "ms"
    if (a >= 1e-6) return tnum(v * 1e6) "µs"
    return tnum(v * 1e9) "ns"
}
function reset() { t=prem=de=ga=ve=rh=th=pg=""; is_pricer=0 }
BEGIN {
    nr = 0   #!< numeric from the start: rows[nr] must index 0,1,2... not the string ""
    printf "%-40s %-11s %10s %12s %10s %10s %10s %10s %10s   %s\n",
           "PRODUCT", "METHOD", "TIME", "PREMIUM", "DELTA", "GAMMA", "VEGA", "RHO", "THETA", "MODEL GREEKS (vega_<param>)"
    printf "%-40s %-11s %10s %12s %10s %10s %10s %10s %10s   %s\n",
           "-------", "------", "----", "-------", "-----", "-----", "----", "---", "-----", "--------------------------"
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
#! model-parameter Greeks (vega_alpha, vega_v0, vega_kappa, ...) — model-specific,
#! so collected into one trailing free-form field rather than fixed columns
name != "" && /^  vega_[a-z0-9_]+:/ {
    key = $1; sub(/:$/, "", key); sub(/^vega_/, "", key)
    pg = pg (pg == "" ? "" : " ") key "=" fmt($2)
}
END {
    flush()
    #! insertion sort of the row indices by their (product, method) key (n ~ 60,
    #! so a simple O(n^2) sort is fine and keeps us in portable POSIX awk)
    for (i = 0; i < nr; i++) ord[i] = i
    for (i = 1; i < nr; i++) {
        k = ord[i]; j = i - 1
        while (j >= 0 && keys[ord[j]] > keys[k]) { ord[j + 1] = ord[j]; j-- }
        ord[j + 1] = k
    }
    #! print sorted, with a blank separator between products so each product's
    #! engine rows (ana / mcl / pde / ...) read as one block
    prev = ""
    for (i = 0; i < nr; i++) {
        split(keys[ord[i]], kp, SUBSEP)   #!< kp[1] = product label
        if (i > 0 && kp[1] != prev) print ""
        print rows[ord[i]]
        prev = kp[1]
    }
    printf "\n%d priced cell(s).\n", nr
}
' "$OUT"
