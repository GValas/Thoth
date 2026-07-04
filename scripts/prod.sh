#!/usr/bin/env bash
# ---------------------------------------------------------------------------
# prod.sh — start (or manage) the full Thoth dashboard stack via docker compose.
#
# Brings up the production stack defined in docker-compose.yml:
#   redis · 2 GPU clusters (c1master/c2master + 5 CPU slaves each) · bff (NestJS) ·
#   web (nginx + SPA) · mcp (the engine as agent tools, Streamable HTTP; nginx proxies it
#   at :7777/mcp — same public port as the dashboard)
#   — then waits until the API and the MCP server are healthy and prints how to reach them.
# Note: the clusters need the NVIDIA Container Toolkit + 2 GPUs (see docker-compose.yml).
#
# Usage:
#   scripts/prod.sh [up]        build images, start, wait for health, then STAY ATTACHED
#                               in the foreground streaming logs. Ctrl-C — or any container
#                               exiting/crashing — tears the WHOLE stack down in cascade.
#   scripts/prod.sh down        stop and remove the stack (keeps named volumes)
#   scripts/prod.sh logs [svc]  follow logs (optionally one service)
#   scripts/prod.sh ps          show service status
#   scripts/prod.sh restart     recreate the stack (detached)
#
# Env / flags:
#   FORCE=1          proceed even if .env still has change-me placeholder secrets
#   NO_BUILD=1       `up` without rebuilding images
#   DETACH=1         `up` returns after the health check instead of staying attached
#                    (the old detached behaviour; no cascade teardown)
# ---------------------------------------------------------------------------
set -euo pipefail

# repo root = parent of this script's dir, so it works from anywhere
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT"

WEB_PORT=7777   # host port the web/nginx service publishes (see docker-compose.yml)
                # nginx also reverse-proxies the MCP server at ${WEB_PORT}/mcp (no own host port)
CMD="${1:-up}"

# --- prerequisites ---------------------------------------------------------
command -v docker >/dev/null 2>&1 || { echo "error: docker not found" >&2; exit 1; }
docker compose version >/dev/null 2>&1 || { echo "error: 'docker compose' (v2) not found" >&2; exit 1; }

compose() { docker compose -f "$ROOT/docker-compose.yml" "$@"; }

case "$CMD" in
  down)    compose down; exit 0 ;;
  logs)    shift || true; compose logs -f --tail=100 "$@"; exit 0 ;;
  ps)      compose ps; exit 0 ;;
  restart) compose up -d --force-recreate; exit 0 ;;
  up)      ;; # fall through to the bring-up below
  *) echo "usage: scripts/prod.sh [up|down|logs|ps|restart]" >&2; exit 2 ;;
esac

# --- secrets / .env --------------------------------------------------------
if [[ ! -f "$ROOT/.env" ]]; then
  cp "$ROOT/.env.example" "$ROOT/.env"
  echo "created .env from .env.example — EDIT THE SECRETS (JWT_*, ADMIN_PASSWORD) then re-run." >&2
  exit 1
fi
if grep -q 'change-me' "$ROOT/.env" && [[ "${FORCE:-0}" != "1" ]]; then
  echo "refusing to start: .env still contains 'change-me' placeholder secrets." >&2
  echo "edit .env (JWT_SECRET, JWT_REFRESH_SECRET, ADMIN_PASSWORD), or re-run with FORCE=1." >&2
  exit 1
fi

# --- bring up (detached first, so we can health-check before attaching) ----
echo "==> starting Thoth stack (redis · spot-feed · 2 GPU clusters [+5 slaves each] · bff · web · mcp)"
if [[ "${NO_BUILD:-0}" == "1" ]]; then compose up -d; else compose up -d --build; fi

# From here on, any failure / interrupt must tear the whole stack down in cascade.
# Disarm the trap inside the handler so a Ctrl-C during teardown can't re-enter it,
# and so the EXIT trap doesn't run `down` a second time after an INT/TERM already did.
teardown() {
  trap - INT TERM EXIT
  echo
  echo "==> tearing down Thoth stack (cascade down)…"
  compose down --remove-orphans
}
trap teardown INT TERM EXIT

# --- wait for health -------------------------------------------------------
echo -n "==> waiting for the API to become healthy"
HEALTHY=0
for _ in $(seq 1 60); do
  if curl -fsS "http://localhost:${WEB_PORT}/api/health" >/dev/null 2>&1; then HEALTHY=1; break; fi
  echo -n "."; sleep 2
done
echo
if [[ "$HEALTHY" != "1" ]]; then
  echo "error: API did not become healthy in time. Recent logs:" >&2
  compose logs --tail=40 bff web || true
  exit 1   # EXIT trap tears the stack down
fi

# --- wait for the MCP server (via nginx on the web port; it only needs its engine cluster) --
echo -n "==> waiting for the MCP server to become healthy"
MCP_HEALTHY=0
for _ in $(seq 1 30); do
  if curl -fsS "http://localhost:${WEB_PORT}/mcp/healthz" >/dev/null 2>&1; then MCP_HEALTHY=1; break; fi
  echo -n "."; sleep 2
done
echo
if [[ "$MCP_HEALTHY" != "1" ]]; then
  echo "error: MCP server did not become healthy in time. Recent logs:" >&2
  compose logs --tail=40 mcp || true
  exit 1   # EXIT trap tears the stack down
fi

ADMIN_EMAIL="$(grep -E '^ADMIN_EMAIL=' "$ROOT/.env" | cut -d= -f2- || true)"
cat <<EOF

==> Thoth dashboard is up.
    URL    : http://localhost:${WEB_PORT}
    API docs: http://localhost:${WEB_PORT}/api/docs
    login  : ${ADMIN_EMAIL:-<ADMIN_EMAIL from .env>} / <ADMIN_PASSWORD from .env>

    MCP    : http://localhost:${WEB_PORT}/mcp   (engine tools for LLM agents; health: /mcp/healthz)
             claude mcp add --transport http thoth-pricing http://localhost:${WEB_PORT}/mcp

    status : scripts/prod.sh ps   (in another shell)
    stop   : Ctrl-C here, or: scripts/prod.sh down
EOF

# --- DETACH=1 keeps the old behaviour: leave the stack running and return ---
if [[ "${DETACH:-0}" == "1" ]]; then
  trap - INT TERM EXIT   # don't tear down on a normal detached exit
  echo "==> detached (DETACH=1): stack left running. Stop it with: scripts/prod.sh down"
  exit 0
fi

# --- stay attached in the foreground ---------------------------------------
# `up --abort-on-container-exit` re-attaches to the already-running stack and streams
# every service's logs. The moment ANY container exits (crash or normal stop) it aborts
# and stops the rest; Ctrl-C does the same. Either way control returns here and the trap
# runs the cascade `down`. This is what keeps prod.sh "holding the hand".
echo "==> attached — streaming logs. Ctrl-C stops the whole stack (cascade down)."
compose up --abort-on-container-exit
