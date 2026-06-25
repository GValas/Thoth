# Thoth — fake real-time spot feed. A tiny Node service that GBM-walks equity spots and
# publishes them to Redis (pub/sub + a latest-snapshot hash) for the BFF to fan out via SSE.
# Build context is web/spot-feed/.

# ---- build stage ----
FROM node:24-bookworm AS build
WORKDIR /app
COPY package.json package-lock.json ./
RUN npm ci --no-audit --no-fund
COPY tsconfig.json ./
COPY src ./src
RUN npm run build

# ---- runtime stage : prod deps + compiled JS only ----
FROM node:24-bookworm-slim AS runtime
WORKDIR /app
ENV NODE_ENV=production
COPY package.json package-lock.json ./
RUN npm ci --omit=dev --no-audit --no-fund && npm cache clean --force
COPY --from=build /app/dist ./dist
CMD ["node", "dist/index.js"]
