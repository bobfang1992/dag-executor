#!/usr/bin/env tsx
/**
 * Manifest sync tool - scans for plan files and updates manifest.json
 *
 * Usage:
 *   tsx dsl/tools/sync_manifest.ts [--dir <dir>] [--manifest <path>]
 *
 * Defaults:
 *   --dir plans
 *   --manifest <dir>/manifest.json
 *
 * The tool scans <dir>/**\/*.plan.ts, sorts lexicographically, and writes
 * a deterministic manifest.json.
 */

import { readdir, writeFile, readFile } from "node:fs/promises";
import { resolve, relative, join } from "node:path";

interface Manifest {
  schema_version: number;
  plans: string[];
}

async function findPlanFiles(dir: string, repoRoot: string): Promise<string[]> {
  const results: string[] = [];

  async function walk(currentDir: string): Promise<void> {
    const entries = await readdir(currentDir, { withFileTypes: true });
    for (const entry of entries) {
      const fullPath = join(currentDir, entry.name);
      if (entry.isDirectory()) {
        await walk(fullPath);
      } else if (entry.isFile() && entry.name.endsWith(".plan.ts")) {
        // Skip test fixture files that are intentionally invalid:
        // - evil*.plan.ts: security test files (attempt eval/Function)
        // - name_mismatch.plan.ts: plan_name != filename test
        const isTestFixture =
          entry.name.startsWith("evil") || entry.name === "name_mismatch.plan.ts";
        if (!isTestFixture) {
          // Use forward slashes for cross-platform consistency
          const relativePath = relative(repoRoot, fullPath).replace(/\\/g, "/");
          results.push(relativePath);
        }
      }
    }
  }

  await walk(dir);
  return results;
}

function stableStringify(obj: unknown): string {
  return JSON.stringify(obj, null, 2) + "\n";
}

async function main(): Promise<void> {
  const args = process.argv.slice(2);

  let dir = "plans";
  let manifestPath: string | null = null;

  for (let i = 0; i < args.length; i++) {
    if (args[i] === "--dir" && i + 1 < args.length) {
      dir = args[++i];
    } else if (args[i] === "--manifest" && i + 1 < args.length) {
      manifestPath = args[++i];
    } else if (args[i] === "--help" || args[i] === "-h") {
      console.log("Usage: sync_manifest.ts [--dir <dir>] [--manifest <path>]");
      console.log("");
      console.log("Scans <dir>/**/*.plan.ts and updates manifest.json");
      console.log("");
      console.log("Options:");
      console.log("  --dir <dir>       Directory to scan (default: plans)");
      console.log("  --manifest <path> Manifest file path (default: <dir>/manifest.json)");
      process.exit(0);
    }
  }

  // Find repo root (where pnpm-workspace.yaml is)
  const repoRoot = process.cwd();

  const absDir = resolve(repoRoot, dir);
  const absManifest = manifestPath
    ? resolve(repoRoot, manifestPath)
    : resolve(absDir, "manifest.json");

  // Find all plan files
  const planFiles = await findPlanFiles(absDir, repoRoot);

  // Sort lexicographically for determinism
  planFiles.sort();

  // Build manifest
  const manifest: Manifest = {
    schema_version: 1,
    plans: planFiles,
  };

  // Check if manifest already exists and is identical
  try {
    const existing = await readFile(absManifest, "utf-8");
    const newContent = stableStringify(manifest);
    if (existing === newContent) {
      console.log(`✓ ${relative(repoRoot, absManifest)} is up to date (${planFiles.length} plans)`);
      return;
    }
  } catch {
    // File doesn't exist, will create
  }

  // Write manifest
  await writeFile(absManifest, stableStringify(manifest), "utf-8");
  console.log(`✓ Updated ${relative(repoRoot, absManifest)} (${planFiles.length} plans)`);
  for (const plan of planFiles) {
    console.log(`  - ${plan}`);
  }
}

main().catch((err) => {
  console.error("Error:", err instanceof Error ? err.message : err);
  process.exit(1);
});
