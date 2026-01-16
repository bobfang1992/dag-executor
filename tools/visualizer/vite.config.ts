import { defineConfig } from 'vite';
import react from '@vitejs/plugin-react';
import path from 'path';
import fs from 'fs';

// Custom plugin to serve plan source files
function servePlanSources() {
  return {
    name: 'serve-plan-sources',
    configureServer(server: any) {
      server.middlewares.use('/sources', (req: any, res: any, next: any) => {
        // Serve .plan.ts files from plans/ or examples/plans/
        const urlPath = req.url?.replace(/^\//, '') || '';
        const possiblePaths = [
          path.resolve(__dirname, '../../plans', urlPath),
          path.resolve(__dirname, '../../examples/plans', urlPath),
        ];

        for (const filePath of possiblePaths) {
          if (fs.existsSync(filePath)) {
            const content = fs.readFileSync(filePath, 'utf-8');
            res.setHeader('Content-Type', 'text/plain; charset=utf-8');
            res.end(content);
            return;
          }
        }
        next();
      });
    },
  };
}

export default defineConfig({
  plugins: [react(), servePlanSources()],
  publicDir: path.resolve(__dirname, '../../artifacts'),
  server: {
    port: 5173,
    open: true,
  },
  build: {
    outDir: 'dist',
    sourcemap: true,
    // Don't copy publicDir to dist in production (artifacts are large)
    copyPublicDir: false,
  },
  // Handle WASM files for quickjs-emscripten and esbuild-wasm
  optimizeDeps: {
    exclude: ['quickjs-emscripten', '@jitl/quickjs-wasmfile-release-sync'],
  },
  // Ensure WASM files are served correctly
  assetsInclude: ['**/*.wasm'],
});
