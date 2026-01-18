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
export declare function initCompiler(options: InitCompilerOptions): Promise<void>;

/**
 * Compile a plan from TypeScript source to JSON artifact.
 *
 * @param source - The TypeScript source code
 * @param planName - The plan name (used for filename validation)
 * @returns Compilation result with success/failure and artifact
 */
export declare function compilePlan(source: string, planName: string): Promise<CompileResult>;
