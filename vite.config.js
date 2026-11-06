import { defineConfig } from 'vite';
import react from '@vitejs/plugin-react';

export default defineConfig({
  root: 'web',
  // Relative URLs, so the site works at / (Docker) and at /<repo>/ (GitHub Pages).
  base: './',
  plugins: [react()],
  // worker.js and seam.js are ES modules.
  worker: { format: 'es' },
  build: {
    // Not web/dist: that folder holds the Emscripten output (seam.js, seam.wasm).
    outDir: '../site',
    emptyOutDir: true,
  },
});
