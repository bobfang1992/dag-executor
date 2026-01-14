#!/usr/bin/env tsx

/**
 * Build-all runner: compiles all plans listed in a manifest file.
 *
 * Usage:
 *   tsx dsl/tools/build_all_plans.ts [options]
 *
 * Options:
 *   --manifest <path>  Manifest file path (default: examples/plans/manifest.json)
 *   --out <dir>        Output directory (default: artifacts/plans)
 *   --backend <type>   Compiler backend: quickjs | node (default: quickjs)
 */

import { readFile } from "node:fs/promises";
import { resolve } from "node:path";
import { spawn } from "node:child_process";
import { fileURLToPath } from "node:url";
import { dirname } from "node:path";

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

async function parseArgs(): Promise<BuildOptions> {
  const args = process.argv.slice(2);
  const options: BuildOptions = {
    manifest: "examples/plans/manifest.json",
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
  --manifest <path>  Manifest file path (default: examples/plans/manifest.json)
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

async function main() {
  try {
    const options = await parseArgs();

    console.log(`Reading manifest: ${options.manifest}`);
    console.log(`Output directory: ${options.out}`);
    console.log(`Compiler backend: ${options.backend}`);
    console.log();

    const manifest = await loadManifest(options.manifest);

    console.log(`Found ${manifest.plans.length} plan(s) to compile\n`);

    let successCount = 0;
    let failureCount = 0;

    for (const planPath of manifest.plans) {
      try {
        await compilePlan(planPath, options.out, options.backend);
        successCount++;
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
