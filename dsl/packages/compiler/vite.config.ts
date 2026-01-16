/**
 * Vite config for building the browser-compatible compiler bundle.
 *
 * This config:
 * 1. Bundles runtime and generated packages into source strings
 * 2. Provides virtual modules for those strings
 * 3. Builds a browser-compatible ESM bundle
 */

import { defineConfig, Plugin } from "vite";
import { resolve } from "path";
import { build } from "esbuild";

/**
 * Plugin that provides virtual modules with pre-bundled source code.
 * The runtime and generated packages are bundled into strings that
 * can be passed to esbuild-wasm at runtime.
 */
function virtualSourcePlugin(): Plugin {
  let runtimeSource: string | null = null;
  let generatedSource: string | null = null;

  return {
    name: "virtual-source",

    async buildStart() {
      // Bundle @ranking-dsl/runtime into a single string
      const runtimeResult = await build({
        entryPoints: [resolve(__dirname, "../runtime/src/index.ts")],
        bundle: true,
        format: "esm",
        platform: "neutral",
        target: "es2020",
        write: false,
        // Don't include generated - we'll provide it separately
        external: ["@ranking-dsl/generated"],
      });
      runtimeSource = runtimeResult.outputFiles[0].text;

      // Bundle @ranking-dsl/generated into a single string
      const generatedResult = await build({
        entryPoints: [resolve(__dirname, "../generated/index.ts")],
        bundle: true,
        format: "esm",
        platform: "neutral",
        target: "es2020",
        write: false,
      });
      generatedSource = generatedResult.outputFiles[0].text;

      console.log(
        `[virtual-source] Bundled runtime (${runtimeSource.length} bytes) and generated (${generatedSource.length} bytes)`
      );
    },

    resolveId(id) {
      if (id === "virtual:runtime-source" || id === "virtual:generated-source") {
        return id;
      }
      return null;
    },

    load(id) {
      if (id === "virtual:runtime-source") {
        if (!runtimeSource) {
          throw new Error("Runtime source not yet bundled");
        }
        // Export the source as a string
        return `export default ${JSON.stringify(runtimeSource)};`;
      }
      if (id === "virtual:generated-source") {
        if (!generatedSource) {
          throw new Error("Generated source not yet bundled");
        }
        return `export default ${JSON.stringify(generatedSource)};`;
      }
      return null;
    },
  };
}

export default defineConfig({
  plugins: [virtualSourcePlugin()],

  build: {
    lib: {
      entry: resolve(__dirname, "src/browser.ts"),
      name: "RankingDSLCompiler",
      formats: ["es"],
      fileName: "browser",
    },

    rollupOptions: {
      // Don't bundle these - they should be loaded separately
      external: ["esbuild-wasm", "quickjs-emscripten"],

      output: {
        // Preserve export names
        exports: "named",
      },
    },

    // Output to dist/
    outDir: "dist",

    // Don't clear dist (tsc outputs there too)
    emptyOutDir: false,

    // Generate sourcemaps for debugging
    sourcemap: true,

    // Don't minify for easier debugging (can change for production)
    minify: false,
  },

  // Ensure we can use top-level await
  esbuild: {
    target: "es2020",
  },
});
