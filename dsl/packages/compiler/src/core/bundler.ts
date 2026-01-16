/**
 * Core bundler - browser-compatible esbuild bundling.
 *
 * This module provides the core bundling logic that works in both Node and browser.
 * It takes source code directly (not file paths) and uses virtual modules.
 */

import type * as esbuildTypes from "esbuild";

export interface BundleOptions {
  /** The plan source code */
  planSource: string;
  /** Filename for the plan (used in error messages) */
  planFilename: string;
  /** Source code for @ranking-dsl/runtime (pre-bundled) */
  runtimeSource: string;
  /** Source code for @ranking-dsl/generated (pre-bundled) */
  generatedSource: string;
  /** esbuild instance (allows using esbuild or esbuild-wasm) */
  esbuild: typeof esbuildTypes;
}

export interface BundleResult {
  code: string;
  warnings: string[];
}

/**
 * Bundle plan source code into a single IIFE script.
 * Uses virtual modules for the plan and DSL packages.
 */
export async function bundlePlanCore(
  options: BundleOptions
): Promise<BundleResult> {
  const { planSource, planFilename, runtimeSource, generatedSource, esbuild } = options;
  const warnings: string[] = [];

  // Virtual module plugin - resolves imports from memory
  const virtualModulePlugin: esbuildTypes.Plugin = {
    name: "virtual-modules",
    setup(build) {
      // Resolve virtual module paths
      build.onResolve({ filter: /^virtual:/ }, (args) => ({
        path: args.path,
        namespace: "virtual",
      }));

      // Resolve @ranking-dsl packages to virtual modules
      build.onResolve({ filter: /^@ranking-dsl\/(runtime|generated)$/ }, (args) => ({
        path: args.path,
        namespace: "virtual",
      }));

      // Load virtual module contents
      build.onLoad({ filter: /.*/, namespace: "virtual" }, (args) => {
        if (args.path === "virtual:plan") {
          return { contents: planSource, loader: "ts" };
        }
        if (args.path === "@ranking-dsl/runtime") {
          return { contents: runtimeSource, loader: "ts" };
        }
        if (args.path === "@ranking-dsl/generated") {
          return { contents: generatedSource, loader: "ts" };
        }
        return null;
      });
    },
  };

  const result = await esbuild.build({
    stdin: {
      contents: `import "virtual:plan";`,
      loader: "ts",
      resolveDir: "/",
      sourcefile: "entry.ts",
    },
    bundle: true,
    format: "iife",
    platform: "neutral",
    target: "es2020",
    write: false,
    logLevel: "silent",
    outfile: "bundle.js",
    define: {
      "process.env.NODE_ENV": '"production"',
    },
    plugins: [virtualModulePlugin],
  });

  // Collect warnings
  if (result.warnings.length > 0) {
    for (const warning of result.warnings) {
      warnings.push(
        `${warning.location?.file ?? planFilename}:${warning.location?.line ?? 0}: ${warning.text}`
      );
    }
  }

  if (result.errors.length > 0) {
    const errorMessages = result.errors.map(
      (err) =>
        `${err.location?.file ?? planFilename}:${err.location?.line ?? 0}: ${err.text}`
    );
    throw new Error(`Bundle failed:\n${errorMessages.join("\n")}`);
  }

  if (result.outputFiles.length !== 1) {
    throw new Error(
      `Expected exactly one output file, got ${result.outputFiles.length}`
    );
  }

  const code = result.outputFiles[0].text;
  return { code, warnings };
}
