# Thoth dashboard — React POC production image: build the Vite app, serve it with nginx
# (which also reverse-proxies /api to the BFF, so the browser sees one same-origin host
# and there is no CORS). Mirrors web/frontend/Dockerfile; build context is web/frontend-react/.

# ---- build stage ----
FROM node:24-bookworm AS build
WORKDIR /app
COPY package.json package-lock.json ./
RUN npm ci --no-audit --no-fund
COPY . .
RUN npm run build

# ---- runtime stage : static files + reverse proxy ----
FROM nginx:1.27-alpine AS runtime
COPY nginx.conf /etc/nginx/conf.d/default.conf
# Vite emits the bundled SPA to dist/
COPY --from=build /app/dist /usr/share/nginx/html
EXPOSE 80
