/**
 * @ranking-dsl/compiler - Plan compiler.
 *
 * Main entry point re-exports from core for Node usage.
 * For browser usage, import from '@ranking-dsl/compiler/browser'.
 */

export {
  compilePlan,
  stableStringify,
  bundlePlanCore,
  executePlan,
} from "./core/index.js";

export type {
  CompileOptions,
  CompileResult,
  CompileSuccess,
  CompileFailure,
  BundleOptions,
  BundleResult,
  ExecuteOptions,
  ExecuteResult,
} from "./core/index.js";
