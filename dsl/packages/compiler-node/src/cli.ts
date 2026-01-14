#!/usr/bin/env node

/**
 * Plan compiler CLI - compiles TypeScript plans to JSON artifacts.
 */

import { resolve } from "node:path";
import { mkdir, writeFile, stat } from "node:fs/promises";
import { pathToFileURL } from "node:url";
import type { PlanDef } from "@ranking-dsl/runtime";
import { PlanCtx } from "@ranking-dsl/runtime";
import { stableStringify } from "./stable-stringify.js";

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

async function compilePlan(planPath: string, force: boolean) {
  const absPath = resolve(process.cwd(), planPath);

  // Derive expected output path from input filename (foo.plan.ts -> foo.plan.json)
  const baseName = planPath.split("/").pop()?.replace(/\.ts$/, ".json") ?? "";
  const outputDir = resolve(process.cwd(), "artifacts/plans");
  const expectedOutput = resolve(outputDir, baseName);

  // Check if incremental skip is possible
  if (!force) {
    const srcMtime = await getMtime(absPath);
    const outMtime = await getMtime(expectedOutput);
    if (srcMtime !== null && outMtime !== null && outMtime >= srcMtime) {
      console.log(`Skipping (up-to-date): ${planPath}`);
      return;
    }
  }

  console.log(`Compiling: ${planPath}`);

  // Import the plan module using dynamic import
  const planUrl = pathToFileURL(absPath).href;
  const module = await import(planUrl);

  // Find the default export (should be PlanDef)
  const planDef: PlanDef = module.default;
  if (!planDef || typeof planDef.build !== "function") {
    throw new Error(
      `Plan module must export a default PlanDef with build() method: ${planPath}`
    );
  }

  // Build the plan
  const ctx = new PlanCtx();
  const result = planDef.build(ctx);
  const artifact = ctx.finalize(result.getNodeId(), planDef.name);

  // Write to artifacts/plans/<plan_name>.plan.json
  await mkdir(outputDir, { recursive: true });

  const outputPath = resolve(outputDir, `${planDef.name}.plan.json`);
  const json = stableStringify(artifact);
  await writeFile(outputPath, json + "\n", "utf-8");

  console.log(`✓ Generated: ${outputPath}`);
}

main().catch((err) => {
  console.error("Error:", err.message);
  console.error(err.stack);
  process.exit(1);
});
