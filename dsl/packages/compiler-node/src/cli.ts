#!/usr/bin/env node

/**
 * Plan compiler CLI - compiles TypeScript plans to JSON artifacts.
 *
 * NOTE: This CLI uses dynamic import() for .plan.ts files, which requires
 * a TypeScript loader (tsx). Run via `pnpm run plan:build` or `tsx cli.ts`.
 * Plain Node.js cannot import .ts files without a loader.
 */

import { resolve, dirname, basename } from "node:path";
import { mkdir, writeFile, stat, readdir, access, readFile } from "node:fs/promises";
import { pathToFileURL, fileURLToPath } from "node:url";
import type { PlanDef } from "@ranking-dsl/runtime";
import { PlanCtx } from "@ranking-dsl/runtime";
import { stableStringify } from "./stable-stringify.js";

// Read package version
const __filename = fileURLToPath(import.meta.url);
let PACKAGE_VERSION = "unknown";
try {
  const packageJsonPath = resolve(dirname(__filename), "../package.json");
  const pkgContent = await readFile(packageJsonPath, "utf-8");
  const pkg = JSON.parse(pkgContent) as { version?: string };
  PACKAGE_VERSION = pkg.version ?? "unknown";
} catch {
  // Ignore errors, use "unknown"
}

// Files that affect all plan outputs (registry + generated tokens + runtime)
// NOTE: This does not track helper modules imported by plans. If a plan imports
// local helpers (e.g., ./utils.ts) and those change, use --force to rebuild.
const DEPENDENCY_PATTERNS = [
  "registry/keys.toml",
  "registry/params.toml",
  "registry/features.toml",
  "dsl/packages/generated",      // generated tokens (*.ts at root)
  "dsl/packages/runtime/src",    // runtime sources
];

let cachedDepMtime: number | null = null;
let cachedRepoRoot: string | null = null;

/**
 * Find repo root by looking for pnpm-workspace.yaml or .git
 */
async function findRepoRoot(): Promise<string> {
  if (cachedRepoRoot !== null) {
    return cachedRepoRoot;
  }

  // Start from CLI file location and walk up
  const __filename = fileURLToPath(import.meta.url);
  let dir = dirname(__filename);

  while (dir !== "/") {
    try {
      await access(resolve(dir, "pnpm-workspace.yaml"));
      cachedRepoRoot = dir;
      return dir;
    } catch {
      // Not found, go up
    }
    dir = dirname(dir);
  }

  // Fallback to cwd if not found
  cachedRepoRoot = process.cwd();
  return cachedRepoRoot;
}

async function main() {
  const args = process.argv.slice(2);
  let force = false;
  let outputDir: string | null = null;
  const planPaths: string[] = [];

  for (let i = 0; i < args.length; i++) {
    if (args[i] === "--force" || args[i] === "-f") {
      force = true;
    } else if (args[i] === "--out") {
      if (i + 1 >= args.length) {
        console.error("Error: --out requires an argument");
        process.exit(1);
      }
      outputDir = args[++i];
    } else if (!args[i].startsWith("-")) {
      planPaths.push(args[i]);
    } else {
      console.error(`Error: Unknown option '${args[i]}'`);
      process.exit(1);
    }
  }

  if (planPaths.length === 0) {
    console.error("Usage: plan-build [--force] [--out <dir>] <plan.ts> [<plan.ts>...]");
    console.error("  --force, -f     Rebuild even if output is up-to-date");
    console.error("  --out <dir>     Output directory (default: artifacts/plans)");
    process.exit(1);
  }

  for (const planPath of planPaths) {
    await compilePlan(planPath, force, outputDir);
  }
}

async function getMtime(path: string): Promise<number | null> {
  try {
    const s = await stat(path);
    return s.mtimeMs;
  } catch {
    return null;
  }
}

async function getMaxMtimeInDir(dirPath: string): Promise<number> {
  let maxMtime = 0;
  try {
    const entries = await readdir(dirPath, { withFileTypes: true });
    for (const entry of entries) {
      const fullPath = resolve(dirPath, entry.name);
      if (entry.isFile() && entry.name.endsWith(".ts")) {
        const mtime = await getMtime(fullPath);
        if (mtime !== null && mtime > maxMtime) {
          maxMtime = mtime;
        }
      }
    }
  } catch {
    // Directory doesn't exist
  }
  return maxMtime;
}

async function getDependencyMtime(): Promise<number> {
  if (cachedDepMtime !== null) {
    return cachedDepMtime;
  }

  const repoRoot = await findRepoRoot();
  let maxMtime = 0;

  for (const pattern of DEPENDENCY_PATTERNS) {
    const fullPath = resolve(repoRoot, pattern);
    const s = await stat(fullPath).catch(() => null);
    if (s?.isDirectory()) {
      const dirMtime = await getMaxMtimeInDir(fullPath);
      if (dirMtime > maxMtime) maxMtime = dirMtime;
    } else if (s?.isFile()) {
      if (s.mtimeMs > maxMtime) maxMtime = s.mtimeMs;
    }
  }

  cachedDepMtime = maxMtime;
  return maxMtime;
}

async function compilePlan(planPath: string, force: boolean, customOutputDir: string | null) {
  const absPath = resolve(process.cwd(), planPath);
  const repoRoot = await findRepoRoot();

  // Use custom output dir if provided, otherwise default to artifacts/plans
  const outputDir = customOutputDir
    ? resolve(process.cwd(), customOutputDir)
    : resolve(repoRoot, "artifacts/plans");

  // Import the plan module first to get the actual plan name
  const planUrl = pathToFileURL(absPath).href;
  const module = await import(planUrl);

  const planDef: PlanDef = module.default;
  if (!planDef || typeof planDef.build !== "function") {
    throw new Error(
      `Plan module must export a default PlanDef with build() method: ${planPath}`
    );
  }

  // Enforce plan_name matches filename
  const planFileName = basename(absPath);
  const expectedName = planFileName.replace(/\.plan\.ts$/, "");
  if (planDef.name !== expectedName) {
    throw new Error(
      `Plan name "${planDef.name}" doesn't match filename "${planFileName}". ` +
      `Rename file to "${planDef.name}.plan.ts" or change plan name to "${expectedName}".`
    );
  }

  // Use actual plan name for output path
  const outputPath = resolve(outputDir, `${planDef.name}.plan.json`);

  // Check if incremental skip is possible (after knowing actual output path)
  if (!force) {
    const srcMtime = await getMtime(absPath);
    const depMtime = await getDependencyMtime();
    const outMtime = await getMtime(outputPath);

    // Rebuild if source or any dependency is newer than output
    const newestInput = Math.max(srcMtime ?? 0, depMtime);
    if (outMtime !== null && outMtime >= newestInput) {
      console.log(`Skipping (up-to-date): ${planPath}`);
      return;
    }
  }

  console.log(`Compiling: ${planPath}`);

  // Build the plan
  const ctx = new PlanCtx();
  const result = planDef.build(ctx);
  const artifact = ctx.finalize(result.getNodeId(), planDef.name);

  // Add built_by metadata
  const artifactWithMetadata = {
    ...artifact,
    built_by: {
      backend: "node",
      tool: "compiler-node",
      tool_version: PACKAGE_VERSION,
    },
  };

  // Write to output directory
  await mkdir(outputDir, { recursive: true });

  const json = stableStringify(artifactWithMetadata);
  await writeFile(outputPath, json + "\n", "utf-8");

  console.log(`✓ Generated: ${outputPath}`);
}

main().catch((err) => {
  console.error("Error:", err.message);
  console.error(err.stack);
  process.exit(1);
});
