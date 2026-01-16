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
 * Initialize the browser compiler.
 * Must be called before compilePlan().
 */
export async function initCompiler(): Promise<void> {
  if (esbuildWasm) return; // Already initialized

  // Dynamic import to avoid bundling issues
  const esbuild = await import("esbuild-wasm");

  // Initialize esbuild-wasm
  // In browser, we need to use wasmURL or wasmModule
  await esbuild.initialize({
    wasmURL: "https://unpkg.com/esbuild-wasm@0.19.12/esbuild.wasm",
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
