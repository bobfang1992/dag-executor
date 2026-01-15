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
import { mkdir, writeFile, access, readFile } from "node:fs/promises";
import { fileURLToPath } from "node:url";
import { createHash } from "node:crypto";
import { bundlePlan } from "./bundler.js";
import { executePlan } from "./executor.js";
import { stableStringify } from "./stable-stringify.js";
import { isValidPlanName } from "@ranking-dsl/generated";
import { validateArtifact } from "@ranking-dsl/runtime";

// Read package version
const __filename = fileURLToPath(import.meta.url);
const __dirname = dirname(__filename);
const packageJsonPath = resolve(__dirname, "../package.json");
let PACKAGE_VERSION = "unknown";
try {
  const pkgContent = await readFile(packageJsonPath, "utf-8");
  const pkg = JSON.parse(pkgContent) as { version?: string };
  PACKAGE_VERSION = pkg.version ?? "unknown";
} catch {
  // Ignore errors, use "unknown"
}

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

    // Step 3: Validate artifact structure (full PlanArtifact schema)
    // Uses shared validation from @ranking-dsl/runtime for parity with Node compiler
    validateArtifact(artifact, planFileName);

    // After validation, we know artifact is a valid object
    const validatedArtifact = artifact as Record<string, unknown>;
    const planName = (validatedArtifact as { plan_name: string }).plan_name;

    // Step 4: Enforce plan_name matches filename and is valid
    const expectedName = planFileName.replace(/\.plan\.ts$/, "");
    if (planName !== expectedName) {
      throw new Error(
        `Plan name "${planName}" doesn't match filename "${planFileName}". ` +
        `Rename file to "${planName}.plan.ts" or change plan name to "${expectedName}".`
      );
    }
    if (!isValidPlanName(planName)) {
      throw new Error(
        `Invalid plan name "${planName}". ` +
        `Plan names must match pattern [A-Za-z0-9_]+ (alphanumeric and underscores only).`
      );
    }

    // Step 5: Add built_by metadata
    const bundleDigest = createHash("sha256").update(code).digest("hex").substring(0, 16);
    const artifactWithMetadata = {
      ...validatedArtifact,
      built_by: {
        backend: "quickjs",
        tool: "dslc",
        tool_version: PACKAGE_VERSION,
        bundle_digest: bundleDigest,
      },
    };

    // Step 6: Write to output directory
    const absOutputDir = resolve(process.cwd(), outputDir);
    await mkdir(absOutputDir, { recursive: true });

    const outputPath = resolve(absOutputDir, `${planName}.plan.json`);
    const json = stableStringify(artifactWithMetadata);
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
