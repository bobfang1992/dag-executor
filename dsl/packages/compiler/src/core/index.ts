/**
 * Core compiler - browser-compatible plan compilation.
 *
 * This module provides the main compilePlan() function that works in both
 * Node and browser environments.
 */

import type * as esbuildTypes from "esbuild";
import { bundlePlanCore } from "./bundler.js";
import { executePlan } from "./executor.js";

// Re-export types and utilities for external use
export { stableStringify } from "./stable-stringify.js";
export type { BundleOptions, BundleResult } from "./bundler.js";
export type { ExecuteOptions, ExecuteResult } from "./executor.js";

// Re-export functions (these are also used internally, so no unused warning)
export { bundlePlanCore, executePlan };

/**
 * Options for compiling a plan.
 */
/**
 * Minimal esbuild interface - supports both esbuild and esbuild-wasm.
 * We only use the build() method, so we type just what we need.
 */
export interface EsbuildLike {
  build(options: esbuildTypes.BuildOptions): Promise<esbuildTypes.BuildResult>;
}

export interface CompileOptions {
  /** The plan TypeScript source code */
  planSource: string;
  /** Filename for the plan (e.g., "my_plan.plan.ts") */
  planFilename: string;
  /** Source code for @ranking-dsl/runtime package */
  runtimeSource: string;
  /** Source code for @ranking-dsl/generated package */
  generatedSource: string;
  /** esbuild instance (esbuild for Node, esbuild-wasm for browser) */
  esbuild: EsbuildLike;
  /** Optional: Tool version for built_by metadata */
  toolVersion?: string;
}

/**
 * Result of a successful compilation.
 */
export interface CompileSuccess {
  success: true;
  /** The compiled plan artifact (JSON-serializable) */
  artifact: Record<string, unknown>;
  /** The bundled JavaScript code */
  bundledCode: string;
  /** Any warnings from bundling */
  warnings: string[];
}

/**
 * Result of a failed compilation.
 */
export interface CompileFailure {
  success: false;
  /** Error message */
  error: string;
  /** Phase where the error occurred */
  phase: "bundle" | "execute" | "validate";
}

export type CompileResult = CompileSuccess | CompileFailure;

/**
 * Compile a plan from TypeScript source to JSON artifact.
 *
 * This is the main entry point for plan compilation. It:
 * 1. Bundles the plan with esbuild (including runtime and generated packages)
 * 2. Executes the bundle in a QuickJS sandbox
 * 3. Captures and validates the emitted artifact
 * 4. Adds built_by metadata
 *
 * @example
 * ```typescript
 * import * as esbuild from 'esbuild';
 * import { compilePlan } from '@ranking-dsl/compiler/core';
 *
 * const result = await compilePlan({
 *   planSource: `import { definePlan } from '@ranking-dsl/runtime'; ...`,
 *   planFilename: 'my_plan.plan.ts',
 *   runtimeSource: runtimeCode,
 *   generatedSource: generatedCode,
 *   esbuild,
 * });
 *
 * if (result.success) {
 *   console.log(result.artifact);
 * } else {
 *   console.error(result.error);
 * }
 * ```
 */
export async function compilePlan(options: CompileOptions): Promise<CompileResult> {
  const { planSource, planFilename, runtimeSource, generatedSource, esbuild, toolVersion } = options;

  // Step 1: Bundle with esbuild
  let bundledCode: string;
  let warnings: string[] = [];

  try {
    const bundleResult = await bundlePlanCore({
      planSource,
      planFilename,
      runtimeSource,
      generatedSource,
      esbuild,
    });
    bundledCode = bundleResult.code;
    warnings = bundleResult.warnings;
  } catch (err) {
    return {
      success: false,
      error: err instanceof Error ? err.message : String(err),
      phase: "bundle",
    };
  }

  // Step 2: Execute in QuickJS sandbox
  let artifact: unknown;

  try {
    const execResult = await executePlan({
      code: bundledCode,
      planFilename,
    });
    artifact = execResult.artifact;
  } catch (err) {
    return {
      success: false,
      error: err instanceof Error ? err.message : String(err),
      phase: "execute",
    };
  }

  // Step 3: Validate artifact structure
  // Note: Basic JSON-serializability is already validated by executor.
  // Full schema validation should be done by the caller (CLI does this via @ranking-dsl/runtime).
  if (artifact === null || typeof artifact !== "object") {
    return {
      success: false,
      error: `Plan emitted non-object artifact: ${typeof artifact}`,
      phase: "validate",
    };
  }

  const artifactObj = artifact as Record<string, unknown>;

  // Step 4: Add built_by metadata
  const bundleDigest = await computeBundleDigest(bundledCode);
  const artifactWithMetadata: Record<string, unknown> = {
    ...artifactObj,
    built_by: {
      backend: "quickjs",
      tool: "dslc",
      tool_version: toolVersion ?? "unknown",
      bundle_digest: bundleDigest,
    },
  };

  return {
    success: true,
    artifact: artifactWithMetadata,
    bundledCode,
    warnings,
  };
}

/**
 * Compute bundle digest using SubtleCrypto (browser-compatible).
 * Returns first 16 characters of SHA-256 hex digest.
 */
async function computeBundleDigest(code: string): Promise<string> {
  // Use SubtleCrypto if available (browser and modern Node)
  if (typeof crypto !== "undefined" && crypto.subtle) {
    const encoder = new TextEncoder();
    const data = encoder.encode(code);
    const hashBuffer = await crypto.subtle.digest("SHA-256", data);
    const hashArray = Array.from(new Uint8Array(hashBuffer));
    const hashHex = hashArray.map((b) => b.toString(16).padStart(2, "0")).join("");
    return hashHex.substring(0, 16);
  }

  // Fallback for older Node (shouldn't happen in practice)
  // Return a placeholder - the CLI will use node:crypto instead
  return "0000000000000000";
}
