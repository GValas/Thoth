#!/usr/bin/env bash
# Devcontainer post-create: report the toolchains and bootstrap the web workspaces.
# Tolerant — a failed web install warns but never blocks container creation (the C++
# engine does not depend on it).
set -u

echo "== toolchains =="
g++ --version | head -1
cmake --version | head -1
node --version 2>/dev/null && npm --version 2>/dev/null || echo "node/npm: not found (node devcontainer feature)"
git --version
claude --version 2>/dev/null || true
(nvcc --version && nvidia-smi -L) 2>/dev/null || echo "CUDA toolkit/GPU not detected (mcl_gpu falls back to CPU)"

echo "== web workspaces (best-effort) =="
( cd web/shared   && npm install --no-audit --no-fund && npm run build ) || echo "WARN: web/shared bootstrap skipped/failed"
( cd web/bff      && npm install --no-audit --no-fund )                  || echo "WARN: web/bff install skipped/failed"
( cd web/frontend && npm install --no-audit --no-fund )                  || echo "WARN: web/frontend install skipped/failed"

cat <<'EOF'
== ready ==
  engine : cmake -B pricer/build pricer && cmake --build pricer/build -j
  bff    : cd web/bff && npm run start:dev      (memory queue; no Redis needed)
  web    : cd web/frontend && npm start         (proxies /api -> :3000)
EOF
