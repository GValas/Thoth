# Thoth BFF (NestJS) — production image.
# IMPORTANT: build with the build context set to web/ (not web/bff/), because the BFF
# depends on the sibling package web/shared via `file:../shared`. The root
# docker-compose.yml sets `context: ./web` and `dockerfile: bff/Dockerfile`.

# ---- build stage : compile @thoth/shared and the BFF (incl. native better-sqlite3) --
FROM node:24-bookworm AS build
WORKDIR /app

# shared first (the BFF resolves it through node_modules/@thoth/shared -> ../shared)
COPY shared/package.json shared/tsconfig.json ./shared/
COPY shared/src ./shared/src
RUN cd shared && npm install --no-audit --no-fund && npm run build

COPY bff/package.json bff/package-lock.json ./bff/
RUN cd bff && npm ci --no-audit --no-fund
COPY bff/ ./bff/
RUN cd bff && npm run build

# ---- runtime stage : node + built artifacts only ----------------------------------
FROM node:24-bookworm-slim AS runtime
WORKDIR /app/bff
ENV NODE_ENV=production
# shared package (with its node_modules) sits beside bff so the file: symlink resolves
COPY --from=build /app/shared /app/shared
COPY --from=build /app/bff/node_modules ./node_modules
COPY --from=build /app/bff/dist ./dist
COPY --from=build /app/bff/package.json ./package.json
RUN mkdir -p data
EXPOSE 3000
CMD ["node", "dist/main.js"]
