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

// Default CDN URL for esbuild WASM (fallback when no local WASM provided)
const DEFAULT_ESBUILD_WASM_URL =
  "https://unpkg.com/esbuild-wasm@0.19.12/esbuild.wasm";

/**
 * Options for initializing the browser compiler.
 */
export interface InitCompilerOptions {
  /**
   * URL to the esbuild.wasm file.
   * If not provided, falls back to CDN (unpkg.com).
   * For offline/CSP-restricted environments, bundle the WASM and provide the URL:
   *   import wasmUrl from 'esbuild-wasm/esbuild.wasm?url';
   *   await initCompiler({ wasmURL: wasmUrl });
   */
  wasmURL?: string;
}

/**
 * Initialize the browser compiler.
 * Must be called before compilePlan().
 *
 * @param options - Optional configuration including wasmURL for offline support
 */
export async function initCompiler(options?: InitCompilerOptions): Promise<void> {
  if (esbuildWasm) return; // Already initialized

  // Dynamic import to avoid bundling issues
  const esbuild = await import("esbuild-wasm");

  // Initialize esbuild-wasm with provided or default WASM URL
  await esbuild.initialize({
    wasmURL: options?.wasmURL ?? DEFAULT_ESBUILD_WASM_URL,
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
