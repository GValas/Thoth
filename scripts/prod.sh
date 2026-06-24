#!/usr/bin/env bash
# ---------------------------------------------------------------------------
# prod.sh — start (or manage) the full Thoth dashboard stack via docker compose.
#
# Brings up the production stack defined in docker-compose.yml:
#   redis · pricer1 · pricer2 (engine replicas) · bff (NestJS) · web (nginx + SPA)
# then waits until the API is healthy and prints how to reach it.
#
# Usage:
#   scripts/prod.sh [up]        build images + start detached, wait for health   (default)
#   scripts/prod.sh down        stop and remove the stack (keeps named volumes)
#   scripts/prod.sh logs [svc]  follow logs (optionally one service)
#   scripts/prod.sh ps          show service status
#   scripts/prod.sh restart     recreate the stack
#
# Env / flags:
#   FORCE=1          proceed even if .env still has change-me placeholder secrets
#   NO_BUILD=1       `up` without rebuilding images
# ---------------------------------------------------------------------------
set -euo pipefail

# repo root = parent of this script's dir, so it works from anywhere
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT"

WEB_PORT=8088   # host port the web/nginx service publishes (see docker-compose.yml)
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

# --- bring up --------------------------------------------------------------
echo "==> starting Thoth stack (redis · pricer1 · pricer2 · bff · web)"
if [[ "${NO_BUILD:-0}" == "1" ]]; then compose up -d; else compose up -d --build; fi

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
  exit 1
fi

ADMIN_EMAIL="$(grep -E '^ADMIN_EMAIL=' "$ROOT/.env" | cut -d= -f2- || true)"
cat <<EOF

==> Thoth dashboard is up.
    URL    : http://localhost:${WEB_PORT}
    API docs: http://localhost:${WEB_PORT}/api/docs
    login  : ${ADMIN_EMAIL:-<ADMIN_EMAIL from .env>} / <ADMIN_PASSWORD from .env>

    logs   : scripts/prod.sh logs        (or: scripts/prod.sh logs bff)
    status : scripts/prod.sh ps
    stop   : scripts/prod.sh down
EOF
