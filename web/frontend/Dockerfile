# Thoth dashboard SPA — production image: build the Angular app, serve it with nginx
# (which also reverse-proxies /api to the BFF, so the browser sees one same-origin host
# and there is no CORS). Build context is web/frontend/.

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
# Angular 18 application builder emits to dist/<project>/browser
COPY --from=build /app/dist/frontend/browser /usr/share/nginx/html
EXPOSE 80
