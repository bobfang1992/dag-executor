#!/usr/bin/env node

/**
 * Plan compiler CLI - compiles TypeScript plans to JSON artifacts.
 *
 * NOTE: This CLI uses dynamic import() for .plan.ts files, which requires
 * a TypeScript loader (tsx). Run via `pnpm run plan:build` or `tsx cli.ts`.
 * Plain Node.js cannot import .ts files without a loader.
 */

import { resolve } from "node:path";
import { mkdir, writeFile, stat, readdir } from "node:fs/promises";
import { pathToFileURL } from "node:url";
import type { PlanDef } from "@ranking-dsl/runtime";
import { PlanCtx } from "@ranking-dsl/runtime";
import { stableStringify } from "./stable-stringify.js";

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

async function main() {
  const args = process.argv.slice(2);
  const force = args.includes("--force") || args.includes("-f");
  const planPaths = args.filter((a) => !a.startsWith("-"));

  if (planPaths.length === 0) {
    console.error("Usage: plan-build [--force] <plan.ts> [<plan.ts>...]");
    console.error("  --force, -f  Rebuild even if output is up-to-date");
    process.exit(1);
  }

  for (const planPath of planPaths) {
    await compilePlan(planPath, force);
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

  const cwd = process.cwd();
  let maxMtime = 0;

  for (const pattern of DEPENDENCY_PATTERNS) {
    const fullPath = resolve(cwd, pattern);
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

async function compilePlan(planPath: string, force: boolean) {
  const absPath = resolve(process.cwd(), planPath);
  const outputDir = resolve(process.cwd(), "artifacts/plans");

  // Import the plan module first to get the actual plan name
  const planUrl = pathToFileURL(absPath).href;
  const module = await import(planUrl);

  const planDef: PlanDef = module.default;
  if (!planDef || typeof planDef.build !== "function") {
    throw new Error(
      `Plan module must export a default PlanDef with build() method: ${planPath}`
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

  // Write to artifacts/plans/<plan_name>.plan.json
  await mkdir(outputDir, { recursive: true });

  const json = stableStringify(artifact);
  await writeFile(outputPath, json + "\n", "utf-8");

  console.log(`✓ Generated: ${outputPath}`);
}

main().catch((err) => {
  console.error("Error:", err.message);
  console.error(err.stack);
  process.exit(1);
});
