/**
 * @ranking-dsl/compiler/browser - Browser-compatible plan compiler.
 *
 * This entry point is designed for browser environments where:
 * - esbuild-wasm is used instead of native esbuild
 * - QuickJS WASM is loaded via singlefile variant (embedded)
 *
 * Usage:
 *   import { initCompiler, compilePlan } from '@ranking-dsl/compiler/browser';
 *
 *   await initCompiler();
 *   const result = await compilePlan(sourceCode, 'my_plan');
 */

import {
  compilePlan as compilePlanCore,
  stableStringify,
  type CompileResult,
  type EsbuildLike,
} from "./core/index.js";

// Re-export utilities
export { stableStringify };
export type { CompileResult };

// Virtual module imports - these are replaced by Vite during browser build
// For tsc, we declare them as external strings
// @ts-ignore - Virtual modules are injected by Vite
import runtimeSource from "virtual:runtime-source";
// @ts-ignore - Virtual modules are injected by Vite
import generatedSource from "virtual:generated-source";

// esbuild-wasm instance (initialized lazily)
let esbuildWasm: EsbuildLike | null = null;

/**
 * Options for initializing the browser compiler.
 */
export interface InitCompilerOptions {
  /**
   * URL to the esbuild.wasm file (required).
   * Must match the installed esbuild-wasm version to avoid JS/WASM mismatch.
   *
   * Example with Vite:
   *   import wasmUrl from 'esbuild-wasm/esbuild.wasm?url';
   *   await initCompiler({ wasmURL: wasmUrl });
   */
  wasmURL: string;
}

/**
 * Initialize the browser compiler.
 * Must be called before compilePlan().
 *
 * @param options - Configuration including wasmURL (required)
 */
export async function initCompiler(options: InitCompilerOptions): Promise<void> {
  if (esbuildWasm) return; // Already initialized

  if (!options?.wasmURL) {
    throw new Error(
      "wasmURL is required. Import the WASM file and pass its URL:\n" +
      "  import wasmUrl from 'esbuild-wasm/esbuild.wasm?url';\n" +
      "  await initCompiler({ wasmURL: wasmUrl });"
    );
  }

  // Dynamic import to avoid bundling issues
  const esbuild = await import("esbuild-wasm");

  // Initialize esbuild-wasm with provided WASM URL
  await esbuild.initialize({
    wasmURL: options.wasmURL,
  });

  // esbuild-wasm's build() is compatible with our EsbuildLike interface
  esbuildWasm = esbuild as EsbuildLike;
}

/**
 * Compile a plan from TypeScript source to JSON artifact.
 *
 * @param source - The TypeScript source code
 * @param planName - The plan name (used for filename validation)
 * @returns Compilation result with success/failure and artifact
 */
export async function compilePlan(
  source: string,
  planName: string
): Promise<CompileResult> {
  if (!esbuildWasm) {
    throw new Error("Compiler not initialized. Call initCompiler() first.");
  }

  return compilePlanCore({
    planSource: source,
    planFilename: `${planName}.plan.ts`,
    runtimeSource: runtimeSource as string,
    generatedSource: generatedSource as string,
    esbuild: esbuildWasm,
    toolVersion: "browser",
  });
}
