# Thoth MCP server — production image (Streamable HTTP service mode).
# IMPORTANT: build with the build context set to web/ (not web/mcp/), because the MCP
# server depends on the sibling package web/shared via `file:../shared`. The root
# docker-compose.yml sets `context: ./web` and `dockerfile: mcp/Dockerfile`.

# ---- build stage : compile @thoth/shared and the MCP server -----------------------
FROM node:24-bookworm AS build
WORKDIR /app

# shared first (the server resolves it through node_modules/@thoth/shared -> ../shared)
COPY shared/package.json shared/tsconfig.json ./shared/
COPY shared/src ./shared/src
RUN cd shared && npm install --no-audit --no-fund && npm run build

COPY mcp/package.json mcp/package-lock.json ./mcp/
RUN cd mcp && npm ci --no-audit --no-fund
COPY mcp/ ./mcp/
RUN cd mcp && npm run build

# ---- runtime stage : node + built artifacts only -----------------------------------
FROM node:24-bookworm-slim AS runtime
WORKDIR /app/mcp
ENV NODE_ENV=production
# shared package (with its node_modules) sits beside mcp so the file: symlink resolves
COPY --from=build /app/shared /app/shared
COPY --from=build /app/mcp/node_modules ./node_modules
COPY --from=build /app/mcp/dist ./dist
COPY --from=build /app/mcp/package.json ./package.json
EXPOSE 3001
CMD ["node", "dist/index.js"]
