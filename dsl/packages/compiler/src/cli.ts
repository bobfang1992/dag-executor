#!/usr/bin/env node

/**
 * dslc - Ranking DSL compiler using QuickJS sandbox.
 *
 * Usage:
 *   dslc build <plan.ts> --out <output-dir>
 *   dslc build <plan.ts> [<plan.ts>...] --out <output-dir>
 *
 * This compiler:
 * 1. Bundles plan TS files with esbuild (single IIFE, no Node builtins)
 * 2. Executes in QuickJS sandbox (no IO, no eval, no Function, no imports)
 * 3. Captures __emitPlan() call and writes artifact to JSON
 */

import { resolve, dirname, basename } from "node:path";
import { mkdir, writeFile, access } from "node:fs/promises";
import { fileURLToPath } from "node:url";
import { bundlePlan } from "./bundler.js";
import { executePlan } from "./executor.js";
import { stableStringify } from "./stable-stringify.js";

async function main() {
  const args = process.argv.slice(2);

  if (args.length === 0 || args[0] === "--help" || args[0] === "-h") {
    console.error("Usage: dslc build <plan.ts> [<plan.ts>...] --out <output-dir>");
    console.error("");
    console.error("Compiles TypeScript plans to JSON artifacts using QuickJS sandbox.");
    console.error("");
    console.error("Options:");
    console.error("  --out <dir>   Output directory for compiled artifacts (required)");
    process.exit(args[0] === "--help" || args[0] === "-h" ? 0 : 1);
  }

  if (args[0] !== "build") {
    console.error(`Error: Unknown command '${args[0]}'`);
    console.error("Usage: dslc build <plan.ts> [<plan.ts>...] --out <output-dir>");
    process.exit(1);
  }

  // Parse arguments
  const planPaths: string[] = [];
  let outputDir: string | null = null;

  for (let i = 1; i < args.length; i++) {
    if (args[i] === "--out") {
      if (i + 1 >= args.length) {
        console.error("Error: --out requires an argument");
        process.exit(1);
      }
      outputDir = args[++i];
    } else if (args[i].startsWith("-")) {
      console.error(`Error: Unknown option '${args[i]}'`);
      process.exit(1);
    } else {
      planPaths.push(args[i]);
    }
  }

  if (planPaths.length === 0) {
    console.error("Error: No plan files specified");
    process.exit(1);
  }

  if (outputDir === null) {
    console.error("Error: --out <output-dir> is required");
    process.exit(1);
  }

  const repoRoot = await findRepoRoot();

  for (const planPath of planPaths) {
    await compilePlan(planPath, outputDir, repoRoot);
  }
}

async function findRepoRoot(): Promise<string> {
  const __filename = fileURLToPath(import.meta.url);
  let dir = dirname(__filename);

  while (dir !== "/") {
    try {
      await access(resolve(dir, "pnpm-workspace.yaml"));
      return dir;
    } catch {
      // Not found, go up
    }
    dir = dirname(dir);
  }

  // Fallback to cwd if not found
  return process.cwd();
}

async function compilePlan(
  planPath: string,
  outputDir: string,
  repoRoot: string
) {
  const absPath = resolve(process.cwd(), planPath);
  const planFileName = basename(absPath);

  console.log(`Compiling: ${planPath}`);

  try {
    // Step 1: Bundle with esbuild
    const { code, warnings } = await bundlePlan({
      entryPoint: absPath,
      repoRoot,
    });

    // Report warnings if any
    if (warnings.length > 0) {
      console.warn(`Warnings during bundling ${planFileName}:`);
      for (const warning of warnings) {
        console.warn(`  ${warning}`);
      }
    }

    // Step 2: Execute in QuickJS sandbox
    const { artifact } = await executePlan({
      code,
      planPath: planFileName,
    });

    // Step 3: Validate artifact structure
    if (
      artifact === null ||
      typeof artifact !== "object" ||
      !("plan_name" in artifact) ||
      typeof artifact.plan_name !== "string"
    ) {
      throw new Error(
        `Invalid artifact from ${planFileName}: missing or invalid plan_name`
      );
    }

    const planName = artifact.plan_name;

    // Step 4: Write to output directory
    const absOutputDir = resolve(process.cwd(), outputDir);
    await mkdir(absOutputDir, { recursive: true });

    const outputPath = resolve(absOutputDir, `${planName}.plan.json`);
    const json = stableStringify(artifact);
    await writeFile(outputPath, json + "\n", "utf-8");

    console.log(`✓ Generated: ${outputPath}`);
  } catch (err) {
    if (err instanceof Error) {
      console.error(`Error compiling ${planFileName}:`);
      console.error(`  ${err.message}`);
      if (err.stack && process.env.DEBUG) {
        console.error(err.stack);
      }
    } else {
      console.error(`Error compiling ${planFileName}:`, err);
    }
    process.exit(1);
  }
}

main().catch((err) => {
  console.error("Fatal error:", err);
  process.exit(1);
});
