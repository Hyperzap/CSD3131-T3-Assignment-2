# Builds the whole site from source and serves it with nginx:
#   docker build -t seam-carving .
#   docker run --rm -p 8080:80 seam-carving
# Then open http://localhost:8080

# 1. C++ -> seam.js + seam.wasm. Keep this version in step with the wasm job in build.yml.
FROM emscripten/emsdk:3.1.74 AS wasm
WORKDIR /app
COPY CMakeLists.txt ./
COPY src/ src/
COPY web/bindings.cpp web/seam.d.ts web/
RUN emcmake cmake -S . -B build-web -DCMAKE_BUILD_TYPE=Release \
    && cmake --build build-web -j

# 2. React app + the WASM module -> static files in site/
FROM node:22-slim AS site
WORKDIR /app
COPY package.json package-lock.json vite.config.js ./
RUN npm ci
COPY web/ web/
COPY --from=wasm /app/web/dist/ web/dist/
RUN npm run build

# 3. Static file server
FROM nginx:stable-alpine
COPY --from=site /app/site/ /usr/share/nginx/html/
EXPOSE 80
