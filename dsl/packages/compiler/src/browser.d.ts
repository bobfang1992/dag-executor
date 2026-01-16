/**
 * @ranking-dsl/compiler/browser - Browser-compatible plan compiler.
 *
 * Type declarations for the browser entry point.
 */

import type { CompileResult } from "./core/index.js";

export { stableStringify } from "./core/stable-stringify.js";
export type { CompileResult };

/**
 * Initialize the browser compiler.
 * Must be called before compilePlan().
 */
export declare function initCompiler(): Promise<void>;

/**
 * Compile a plan from TypeScript source to JSON artifact.
 *
 * @param source - The TypeScript source code
 * @param planName - The plan name (used for filename validation)
 * @returns Compilation result with success/failure and artifact
 */
export declare function compilePlan(source: string, planName: string): Promise<CompileResult>;
