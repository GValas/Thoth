#!/usr/bin/env bash
# ---------------------------------------------------------------------------
# prod-react.sh — start the full Thoth stack but serve the REACT frontend instead of
# the Angular one. Identical to scripts/prod.sh in every other respect (same redis, GPU
# clusters, BFF, ports, health check, foreground teardown) — it just layers
# docker-compose.react.yml on top, which rebuilds the `web` service from web/frontend-react.
#
# Usage / flags are exactly prod.sh's:
#   scripts/prod-react.sh [up|down|logs|ps|restart]
#   FORCE=1  NO_BUILD=1  DETACH=1
#
# The backend (BFF + engines) is untouched, so the React SPA talks to the same API on the
# same URL (http://localhost:7777).
# ---------------------------------------------------------------------------
set -euo pipefail
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
exec env COMPOSE_EXTRA=docker-compose.react.yml "$HERE/prod.sh" "$@"
