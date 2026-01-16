#!/usr/bin/env tsx

/**
 * Build-all runner: compiles all plans listed in a manifest file.
 *
 * Usage:
 *   tsx dsl/tools/build_all_plans.ts [options]
 *
 * Options:
 *   --manifest <path>  Manifest file path (default: plans/manifest.json)
 *   --out <dir>        Output directory (default: artifacts/plans)
 *   --backend <type>   Compiler backend: quickjs | node (default: quickjs)
 */

import { readFile, readdir, writeFile, mkdir } from "node:fs/promises";
import { resolve, basename } from "node:path";
import { spawn } from "node:child_process";
import { fileURLToPath } from "node:url";
import { dirname } from "node:path";
import { createHash } from "node:crypto";

const __filename = fileURLToPath(import.meta.url);
const __dirname = dirname(__filename);
const REPO_ROOT = resolve(__dirname, "../..");

interface Manifest {
  schema_version: number;
  plans: string[];
}

interface BuildOptions {
  manifest: string;
  out: string;
  backend: "quickjs" | "node";
}

interface IndexEntry {
  name: string;
  path: string;
  digest: string;
  capabilities_digest: string;
  built_by: {
    backend: string;
    tool: string;
    tool_version: string;
  };
}

interface PlanIndex {
  schema_version: number;
  plans: IndexEntry[];
}

async function parseArgs(): Promise<BuildOptions> {
  const args = process.argv.slice(2);
  const options: BuildOptions = {
    manifest: "plans/manifest.json",
    out: "artifacts/plans",
    backend: "quickjs",
  };

  for (let i = 0; i < args.length; i++) {
    switch (args[i]) {
      case "--manifest":
        if (i + 1 >= args.length) {
          throw new Error("--manifest requires an argument");
        }
        options.manifest = args[++i];
        break;
      case "--out":
        if (i + 1 >= args.length) {
          throw new Error("--out requires an argument");
        }
        options.out = args[++i];
        break;
      case "--backend":
        if (i + 1 >= args.length) {
          throw new Error("--backend requires an argument");
        }
        const backend = args[++i];
        if (backend !== "quickjs" && backend !== "node") {
          throw new Error(`Invalid backend: ${backend}. Must be 'quickjs' or 'node'`);
        }
        options.backend = backend;
        break;
      case "--help":
      case "-h":
        console.log(`
Build-all runner: compiles all plans listed in a manifest file.

Usage:
  tsx dsl/tools/build_all_plans.ts [options]

Options:
  --manifest <path>  Manifest file path (default: plans/manifest.json)
  --out <dir>        Output directory (default: artifacts/plans)
  --backend <type>   Compiler backend: quickjs | node (default: quickjs)
  --help, -h         Show this help message
`);
        process.exit(0);
      default:
        throw new Error(`Unknown option: ${args[i]}`);
    }
  }

  return options;
}

async function loadManifest(manifestPath: string): Promise<Manifest> {
  const absPath = resolve(REPO_ROOT, manifestPath);
  const content = await readFile(absPath, "utf-8");
  const manifest = JSON.parse(content) as Manifest;

  if (manifest.schema_version !== 1) {
    throw new Error(
      `Unsupported manifest schema version: ${manifest.schema_version}`
    );
  }

  if (!Array.isArray(manifest.plans)) {
    throw new Error("Manifest must have a 'plans' array");
  }

  return manifest;
}

async function compilePlan(
  planPath: string,
  outputDir: string,
  backend: "quickjs" | "node"
): Promise<void> {
  const absOutputDir = resolve(REPO_ROOT, outputDir);

  if (backend === "quickjs") {
    // Use dslc (QuickJS-based compiler)
    const command = "node";
    const args = [
      "dsl/packages/compiler/dist/cli.js",
      "build",
      planPath,
      "--out",
      absOutputDir,
    ];

    console.log(`[QuickJS] Compiling: ${planPath}`);
    await runCommand(command, args, REPO_ROOT);
  } else {
    // Use compiler-node (legacy Node-based compiler)
    const command = "tsx";
    const args = [
      "dsl/packages/compiler-node/src/cli.ts",
      planPath,
      "--out",
      absOutputDir,
    ];

    console.log(`[Node] Compiling: ${planPath}`);
    await runCommand(command, args, REPO_ROOT);
  }
}

function runCommand(
  command: string,
  args: string[],
  cwd: string
): Promise<void> {
  return new Promise((resolve, reject) => {
    const child = spawn(command, args, {
      cwd,
      stdio: "inherit",
      shell: false,
    });

    child.on("error", (err) => {
      reject(new Error(`Failed to spawn ${command}: ${err.message}`));
    });

    child.on("close", (code) => {
      if (code === 0) {
        resolve();
      } else {
        reject(
          new Error(`${command} ${args.join(" ")} exited with code ${code}`)
        );
      }
    });
  });
}

/**
 * Stable stringify for computing deterministic digest.
 * No whitespace, stable key ordering.
 *
 * NOTE: Float formatting may differ between JS (JSON.stringify) and C++ (nlohmann::json::dump)
 * for edge cases. Current capability payloads don't use floats. If floats are needed in the
 * future, implement JCS or align formatting with C++ implementation.
 */
function stableStringifyForDigest(obj: unknown): string {
  if (obj === null || typeof obj !== "object") {
    return JSON.stringify(obj);
  }
  if (Array.isArray(obj)) {
    return "[" + obj.map(stableStringifyForDigest).join(",") + "]";
  }
  const keys = Object.keys(obj).sort();
  const pairs = keys.map(
    (k) => `${JSON.stringify(k)}:${stableStringifyForDigest((obj as Record<string, unknown>)[k])}`
  );
  return "{" + pairs.join(",") + "}";
}

/**
 * Compute capabilities digest for RFC0001.
 * Returns sha256:<hex> of canonical JSON, or "" if both fields are empty/absent.
 *
 * Canonical form: {"capabilities_required":[...],"extensions":{...}}
 * - Keys sorted alphabetically ("capabilities_required" < "extensions")
 * - Empty arrays/objects normalized to [] and {}
 */
function computeCapabilitiesDigest(
  capabilitiesRequired: string[] | undefined,
  extensions: Record<string, unknown> | undefined
): string {
  // Normalize to empty if absent
  const caps = capabilitiesRequired ?? [];
  const exts = extensions ?? {};

  // If both are empty, return empty string (no capabilities)
  if (caps.length === 0 && Object.keys(exts).length === 0) {
    return "";
  }

  // Build canonical object with sorted keys
  const canonical = {
    capabilities_required: caps,
    extensions: exts,
  };

  // Compute digest of canonical JSON
  const hash = createHash("sha256")
    .update(stableStringifyForDigest(canonical))
    .digest("hex");

  return `sha256:${hash}`;
}

/**
 * Generate index.json for the plan store.
 * Only includes plans that were successfully compiled (from compiledPlanNames),
 * not all .plan.json files in the output directory. This ensures manifest is SSOT.
 */
async function generateIndex(
  outputDir: string,
  backend: "quickjs" | "node",
  compiledPlanNames: string[]
): Promise<void> {
  const absOutputDir = resolve(REPO_ROOT, outputDir);

  if (compiledPlanNames.length === 0) {
    console.log("No plans compiled, skipping index generation");
    return;
  }

  const entries: IndexEntry[] = [];

  for (const planName of compiledPlanNames) {
    const file = `${planName}.plan.json`;
    const filePath = resolve(absOutputDir, file);

    try {
      const content = await readFile(filePath, "utf-8");
      const plan = JSON.parse(content) as {
        plan_name: string;
        built_by?: { tool_version?: string };
        capabilities_required?: string[];
        extensions?: Record<string, unknown>;
      };

      // Compute digest of the canonical JSON (no whitespace, stable key order)
      const digest = createHash("sha256")
        .update(stableStringifyForDigest(JSON.parse(content)))
        .digest("hex");

      // Compute capabilities digest (RFC0001)
      const capabilitiesDigest = computeCapabilitiesDigest(
        plan.capabilities_required,
        plan.extensions
      );

      entries.push({
        name: plan.plan_name,
        path: file,
        digest: `sha256:${digest}`,
        capabilities_digest: capabilitiesDigest,
        built_by: {
          backend,
          tool: backend === "quickjs" ? "dslc" : "compiler-node",
          tool_version: plan.built_by?.tool_version ?? "0.1.0",
        },
      });
    } catch (err) {
      console.warn(`Warning: Could not read ${file} for index: ${err instanceof Error ? err.message : err}`);
    }
  }

  // Sort by name for determinism
  entries.sort((a, b) => a.name.localeCompare(b.name));

  const index: PlanIndex = {
    schema_version: 1,
    plans: entries,
  };

  // Write index with stable formatting (2-space indent)
  const indexPath = resolve(absOutputDir, "index.json");
  await writeFile(indexPath, JSON.stringify(index, null, 2) + "\n", "utf-8");
  console.log(`Generated index: ${outputDir}/index.json (${entries.length} plans)`);
}

async function main() {
  try {
    const options = await parseArgs();

    console.log(`Reading manifest: ${options.manifest}`);
    console.log(`Output directory: ${options.out}`);
    console.log(`Compiler backend: ${options.backend}`);
    console.log();

    const manifest = await loadManifest(options.manifest);

    console.log(`Found ${manifest.plans.length} plan(s) to compile\n`);

    // Ensure output directory exists
    const absOutputDir = resolve(REPO_ROOT, options.out);
    await mkdir(absOutputDir, { recursive: true });

    let successCount = 0;
    let failureCount = 0;
    const compiledPlanNames: string[] = [];

    for (const planPath of manifest.plans) {
      try {
        await compilePlan(planPath, options.out, options.backend);
        successCount++;

        // Extract plan name from path: "plans/reels_plan_a.plan.ts" -> "reels_plan_a"
        const planBasename = planPath.split("/").pop()?.replace(".plan.ts", "") ?? "";
        if (planBasename) {
          compiledPlanNames.push(planBasename);
        }
      } catch (err) {
        console.error(`\nError compiling ${planPath}:`);
        if (err instanceof Error) {
          console.error(`  ${err.message}`);
        } else {
          console.error(`  ${err}`);
        }
        failureCount++;
      }
    }

    console.log();

    // Generate index.json only for plans in the manifest (not stale artifacts)
    if (compiledPlanNames.length > 0) {
      await generateIndex(options.out, options.backend, compiledPlanNames);
    }

    console.log();
    console.log("=" .repeat(60));
    console.log(`Compilation complete:`);
    console.log(`  Success: ${successCount}`);
    console.log(`  Failure: ${failureCount}`);
    console.log("=".repeat(60));

    if (failureCount > 0) {
      process.exit(1);
    }
  } catch (err) {
    if (err instanceof Error) {
      console.error(`Fatal error: ${err.message}`);
    } else {
      console.error(`Fatal error: ${err}`);
    }
    process.exit(1);
  }
}

main();
