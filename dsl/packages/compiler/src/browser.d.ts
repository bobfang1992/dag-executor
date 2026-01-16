/**
 * @ranking-dsl/compiler/browser - Browser-compatible plan compiler.
 *
 * Type declarations for the browser entry point.
 */

import type { CompileResult } from "./core/index.js";

export { stableStringify } from "./core/stable-stringify.js";
export type { CompileResult };

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
export declare function initCompiler(options?: InitCompilerOptions): Promise<void>;

/**
 * Compile a plan from TypeScript source to JSON artifact.
 *
 * @param source - The TypeScript source code
 * @param planName - The plan name (used for filename validation)
 * @returns Compilation result with success/failure and artifact
 */
export declare function compilePlan(source: string, planName: string): Promise<CompileResult>;
