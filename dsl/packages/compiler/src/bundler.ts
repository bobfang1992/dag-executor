/**
 * Bundler - uses esbuild to bundle plan TS files into single IIFE scripts.
 */

import * as esbuild from "esbuild";
import { resolve } from "node:path";

export interface BundleOptions {
  entryPoint: string;
  repoRoot: string;
}

export interface BundleResult {
  code: string;
  warnings: string[];
}

/**
 * Bundle a plan file and all its dependencies into a single IIFE script.
 * The bundle is host-agnostic (no Node builtins) and ready for QuickJS execution.
 */
export async function bundlePlan(
  options: BundleOptions
): Promise<BundleResult> {
  const warnings: string[] = [];

  const result = await esbuild.build({
    entryPoints: [options.entryPoint],
    bundle: true,
    format: "iife",
    platform: "neutral", // Avoid Node builtins
    target: "es2020",
    write: false,
    logLevel: "silent",
    outfile: "bundle.js",
    // Ensure process.env references don't break (though they should not exist)
    define: {
      "process.env.NODE_ENV": '"production"',
    },
    // Alias @ranking-dsl packages to their source locations
    alias: {
      "@ranking-dsl/runtime": resolve(
        options.repoRoot,
        "dsl/packages/runtime/src/index.ts"
      ),
      "@ranking-dsl/generated": resolve(
        options.repoRoot,
        "dsl/packages/generated/index.ts"
      ),
    },
  });

  // Collect warnings
  if (result.warnings.length > 0) {
    for (const warning of result.warnings) {
      warnings.push(
        `${warning.location?.file ?? "unknown"}:${warning.location?.line ?? 0}: ${warning.text}`
      );
    }
  }

  if (result.errors.length > 0) {
    const errorMessages = result.errors.map(
      (err) =>
        `${err.location?.file ?? "unknown"}:${err.location?.line ?? 0}: ${err.text}`
    );
    throw new Error(
      `esbuild failed:\n${errorMessages.join("\n")}`
    );
  }

  if (result.outputFiles.length !== 1) {
    throw new Error(
      `Expected exactly one output file, got ${result.outputFiles.length}`
    );
  }

  const code = result.outputFiles[0].text;
  return { code, warnings };
}
