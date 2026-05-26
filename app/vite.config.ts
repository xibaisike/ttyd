import { defineConfig } from 'vite';
import react from '@vitejs/plugin-react-swc';

/**
 * Embedding in the ttyd binary (src/html.h) is not produced by this Vite pipeline.
 * To run against a local ttyd (default port 7681) with hashed assets:
 *   ttyd -I /path/to/app/dist/index.html -D /path/to/app/dist …
 *
 * Relative asset URLs work with `base: './'` for subdirectory installs.
 */
export default defineConfig({
  plugins: [react()],
  base: './',
  server: {
    port: 9000,
    proxy: {
      '/api': { target: 'http://localhost:7681', changeOrigin: true },
      '/token': { target: 'http://localhost:7681', changeOrigin: true },
      '/ws': { target: 'ws://localhost:7681', ws: true },
    },
  },
  build: {
    sourcemap: true,
  },
});
