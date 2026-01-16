/**
 * Browser entry point for the plan compiler.
 *
 * This module provides a browser-compatible compilePlan() function that uses
 * esbuild-wasm and quickjs-emscripten (both WASM-based).
 *
 * The runtime and generated package sources are inlined at build time.
 */

import * as esbuild from "esbuild-wasm";
import { compilePlan as compilePlanCore } from "./core/index.js";
import type { CompileResult } from "./core/index.js";

// These will be replaced at build time with the actual source code
// Using virtual modules defined in vite.config.ts
import RUNTIME_SOURCE from "virtual:runtime-source";
import GENERATED_SOURCE from "virtual:generated-source";

export type { CompileResult, CompileSuccess, CompileFailure } from "./core/index.js";

let initialized = false;
let initPromise: Promise<void> | null = null;

/**
 * Initialize the compiler (loads WASM modules).
 * Call this early to avoid delay on first compile.
 * Safe to call multiple times - will only initialize once.
 */
export async function initCompiler(): Promise<void> {
  if (initialized) return;

  if (initPromise) {
    return initPromise;
  }

  initPromise = (async () => {
    // Initialize esbuild-wasm
    // The wasmURL should be configured by the consuming application
    await esbuild.initialize({
      wasmURL: "/esbuild.wasm",
    });
    initialized = true;
  })();

  return initPromise;
}

/**
 * Check if the compiler has been initialized.
 */
export function isInitialized(): boolean {
  return initialized;
}

/**
 * Compile a plan from TypeScript source code.
 *
 * @param planSource - The TypeScript source code of the plan
 * @param planName - The plan name (should match the name in definePlan())
 * @returns CompileResult with either success + artifact or failure + error
 *
 * @example
 * ```typescript
 * import { initCompiler, compilePlan } from '@ranking-dsl/compiler/browser';
 *
 * await initCompiler();
 *
 * const result = await compilePlan(`
 *   import { definePlan } from '@ranking-dsl/runtime';
 *   import { Key } from '@ranking-dsl/generated';
 *
 *   export default definePlan({
 *     name: 'my_plan',
 *     build: (c, ctx) => {
 *       return ctx.viewer.follow({ fanout: 100 }).take({ limit: 10 });
 *     },
 *   });
 * `, 'my_plan');
 *
 * if (result.success) {
 *   console.log(result.artifact);
 * }
 * ```
 */
export async function compilePlan(
  planSource: string,
  planName: string
): Promise<CompileResult> {
  // Auto-initialize if needed
  if (!initialized) {
    await initCompiler();
  }

  const planFilename = `${planName}.plan.ts`;

  return compilePlanCore({
    planSource,
    planFilename,
    runtimeSource: RUNTIME_SOURCE,
    generatedSource: GENERATED_SOURCE,
    esbuild,
    toolVersion: "0.1.0-browser",
  });
}

/**
 * Get the embedded runtime source code.
 * Useful for providing type definitions to an editor.
 */
export function getRuntimeSource(): string {
  return RUNTIME_SOURCE;
}

/**
 * Get the embedded generated source code.
 * Useful for providing type definitions to an editor.
 */
export function getGeneratedSource(): string {
  return GENERATED_SOURCE;
}
