import { defineConfig } from 'vite';
import react from '@vitejs/plugin-react';
import path from 'path';
import fs from 'fs';
import { spawn } from 'child_process';
import { tmpdir } from 'os';
import { randomUUID } from 'crypto';

// Serve local public files (favicon, etc.)
function serveLocalPublic() {
  return {
    name: 'serve-local-public',
    configureServer(server: any) {
      server.middlewares.use((req: any, res: any, next: any) => {
        const urlPath = req.url?.split('?')[0] || '';
        const localPath = path.resolve(__dirname, 'public', urlPath.slice(1));
        if (fs.existsSync(localPath) && fs.statSync(localPath).isFile()) {
          const ext = path.extname(localPath);
          const mimeTypes: Record<string, string> = {
            '.svg': 'image/svg+xml',
            '.png': 'image/png',
            '.ico': 'image/x-icon',
          };
          res.setHeader('Content-Type', mimeTypes[ext] || 'application/octet-stream');
          fs.createReadStream(localPath).pipe(res);
          return;
        }
        next();
      });
    },
  };
}

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

// Compile API endpoint - uses the real dslc CLI for full parity
function compileApi() {
  const repoRoot = path.resolve(__dirname, '../..');
  const dslcPath = path.resolve(repoRoot, 'dsl/packages/compiler/dist/cli.js');

  return {
    name: 'compile-api',
    configureServer(server: any) {
      server.middlewares.use('/api/compile', async (req: any, res: any, next: any) => {
        if (req.method !== 'POST') {
          return next();
        }

        // Parse JSON body
        let body = '';
        for await (const chunk of req) {
          body += chunk;
        }

        let source: string;
        let filename: string;
        try {
          const parsed = JSON.parse(body);
          source = parsed.source;
          // Sanitize filename to prevent path traversal attacks
          filename = path.basename(parsed.filename || 'live_plan.plan.ts');
        } catch {
          res.statusCode = 400;
          res.setHeader('Content-Type', 'application/json');
          res.end(JSON.stringify({ success: false, error: 'Invalid JSON body', phase: 'parse' }));
          return;
        }

        // Write source to temp file
        const tempDir = path.join(tmpdir(), 'visualizer-compile');
        fs.mkdirSync(tempDir, { recursive: true });
        const tempFile = path.join(tempDir, filename);
        const outDir = path.join(tempDir, 'out-' + randomUUID());
        fs.mkdirSync(outDir, { recursive: true });

        try {
          fs.writeFileSync(tempFile, source, 'utf-8');

          // Run dslc CLI
          const result = await new Promise<{ success: boolean; artifact?: any; error?: string; phase?: string }>((resolve) => {
            const proc = spawn('node', [dslcPath, 'build', tempFile, '--out', outDir], {
              cwd: repoRoot,
              env: { ...process.env, NODE_OPTIONS: '' }, // Clear NODE_OPTIONS to avoid conflicts
            });

            let stderr = '';
            proc.stderr.on('data', (data) => {
              stderr += data.toString();
            });

            proc.on('close', (code) => {
              if (code === 0) {
                // Read the output artifact
                const planName = filename.replace(/\.plan\.ts$/, '');
                const artifactPath = path.join(outDir, `${planName}.plan.json`);
                if (fs.existsSync(artifactPath)) {
                  const artifact = JSON.parse(fs.readFileSync(artifactPath, 'utf-8'));
                  resolve({ success: true, artifact });
                } else {
                  resolve({ success: false, error: 'Artifact not found after compilation', phase: 'output' });
                }
              } else {
                // Parse error from stderr
                const errorMsg = stderr.trim() || `Compilation failed with exit code ${code}`;
                resolve({ success: false, error: errorMsg, phase: 'compile' });
              }
            });

            proc.on('error', (err) => {
              resolve({ success: false, error: err.message, phase: 'spawn' });
            });
          });

          res.setHeader('Content-Type', 'application/json');
          res.end(JSON.stringify(result));
        } finally {
          // Cleanup temp files
          try {
            fs.rmSync(tempFile, { force: true });
            fs.rmSync(outDir, { recursive: true, force: true });
          } catch {
            // Ignore cleanup errors
          }
        }
      });
    },
  };
}

export default defineConfig({
  plugins: [react(), serveLocalPublic(), servePlanSources(), compileApi()],
  publicDir: path.resolve(__dirname, '../../artifacts'),
  server: {
    port: 5175,
    open: true,
  },
  build: {
    outDir: 'dist',
    sourcemap: true,
    // Don't copy publicDir to dist in production (artifacts are large)
    copyPublicDir: false,
  },
});
