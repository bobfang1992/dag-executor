#!/usr/bin/env node

/**
 * Plan compiler CLI - compiles TypeScript plans to JSON artifacts.
 */

import { resolve } from "node:path";
import { mkdir, writeFile } from "node:fs/promises";
import { pathToFileURL } from "node:url";
import type { PlanDef } from "@ranking-dsl/runtime";
import { PlanCtx } from "@ranking-dsl/runtime";
import { stableStringify } from "./stable-stringify.js";

async function main() {
  const args = process.argv.slice(2);
  if (args.length === 0) {
    console.error("Usage: plan-build <plan.ts> [<plan.ts>...]");
    process.exit(1);
  }

  for (const planPath of args) {
    await compilePlan(planPath);
  }
}

async function compilePlan(planPath: string) {
  const absPath = resolve(process.cwd(), planPath);
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
  const outputDir = resolve(process.cwd(), "artifacts/plans");
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
