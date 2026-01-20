// Codegen entry point - orchestrates code generation from registries
// Module breakdown:
//   codegen/types.ts    - Type definitions and type guards
//   codegen/parsers.ts  - TOML parsing functions
//   codegen/utils.ts    - Utility functions (digest, formatting)
//   codegen/gen-ts.ts   - TypeScript generators
//   codegen/gen-cpp.ts  - C++ header generators
//   codegen/gen-monaco.ts - Monaco type definitions generator

import { readFileSync, writeFileSync, existsSync } from "fs";
import path from "path";

// Import all generators and parsers from modules
import {
  // Parsers
  parseKeys,
  parseParams,
  parseFeatures,
  parseValidation,
  parseCapabilities,
  parseTasks,
  // Utils
  computeDigest,
  formatCpp,
  // TypeScript generators
  generateKeysTs,
  generateParamsTs,
  generateFeaturesTs,
  generateValidationTs,
  generateCapabilitiesTs,
  generateTasksTs,
  generateTaskImplTs,
  generateIndexTs,
  generatePlanGlobalsDts,
  // C++ generators
  generateKeysH,
  generateParamsH,
  generateFeaturesH,
  generateValidationH,
  generateCapabilitiesH,
  // Monaco generator
  generateMonacoTypes,
} from "./codegen/index.js";

// =====================================================
// Main
// =====================================================

function main() {
  const checkMode = process.argv.includes("--check");
  const repoRoot = path.resolve(import.meta.dirname, "../..");

  // Parse registries
  const keys = parseKeys(path.join(repoRoot, "registry/keys.toml"));
  const params = parseParams(path.join(repoRoot, "registry/params.toml"));
  const features = parseFeatures(path.join(repoRoot, "registry/features.toml"));
  const validation = parseValidation(path.join(repoRoot, "registry/validation.toml"));
  const capabilities = parseCapabilities(path.join(repoRoot, "registry/capabilities.toml"));
  const tasks = parseTasks(path.join(repoRoot, "registry/tasks.toml"));

  // Build canonical JSON
  const keysCanonical = { schema_version: 1, entries: keys };
  const paramsCanonical = { schema_version: 1, entries: params };
  const featuresCanonical = { schema_version: 1, entries: features };
  const validationCanonical = { schema_version: 1, ...validation };
  const capabilitiesCanonical = { schema_version: 1, entries: capabilities };

  const keysDigest = computeDigest(keysCanonical);
  const paramsDigest = computeDigest(paramsCanonical);
  const featuresDigest = computeDigest(featuresCanonical);
  const validationDigest = computeDigest(validationCanonical);
  const capabilitiesDigest = computeDigest(capabilitiesCanonical);

  // Generate all outputs
  const outputs: Array<{ path: string; content: string }> = [
    // Artifacts JSON
    { path: "artifacts/keys.json", content: JSON.stringify(keysCanonical, null, 2) + "\n" },
    { path: "artifacts/params.json", content: JSON.stringify(paramsCanonical, null, 2) + "\n" },
    { path: "artifacts/features.json", content: JSON.stringify(featuresCanonical, null, 2) + "\n" },
    { path: "artifacts/capabilities.json", content: JSON.stringify(capabilitiesCanonical, null, 2) + "\n" },
    // Artifacts digests
    { path: "artifacts/keys.digest", content: keysDigest + "\n" },
    { path: "artifacts/params.digest", content: paramsDigest + "\n" },
    { path: "artifacts/features.digest", content: featuresDigest + "\n" },
    { path: "artifacts/capabilities.digest", content: capabilitiesDigest + "\n" },
    // TypeScript
    { path: "dsl/packages/generated/keys.ts", content: generateKeysTs(keys, keysDigest) },
    { path: "dsl/packages/generated/params.ts", content: generateParamsTs(params, paramsDigest) },
    { path: "dsl/packages/generated/features.ts", content: generateFeaturesTs(features, featuresDigest) },
    { path: "dsl/packages/generated/validation.ts", content: generateValidationTs(validation, validationDigest) },
    { path: "dsl/packages/generated/capabilities.ts", content: generateCapabilitiesTs(capabilities, capabilitiesDigest) },
    { path: "dsl/packages/generated/tasks.ts", content: generateTasksTs(tasks) },
    { path: "dsl/packages/generated/task-impl.ts", content: generateTaskImplTs(tasks) },
    { path: "dsl/packages/generated/monaco-types.ts", content: generateMonacoTypes(keys, params, tasks) },
    { path: "dsl/packages/generated/index.ts", content: generateIndexTs() },
    // Plan globals declaration for editor support
    { path: "plan-globals.d.ts", content: generatePlanGlobalsDts() },
    // C++ headers (formatted with clang-format)
    // Note: We use *_registry.h names to avoid shadowing system headers (e.g. <features.h>)
    { path: "engine/include/key_registry.h", content: formatCpp(generateKeysH(keys, keysDigest)) },
    { path: "engine/include/param_registry.h", content: formatCpp(generateParamsH(params, paramsDigest)) },
    { path: "engine/include/feature_registry.h", content: formatCpp(generateFeaturesH(features, featuresDigest)) },
    { path: "engine/include/validation.h", content: formatCpp(generateValidationH(validation, validationDigest)) },
    { path: "engine/include/capability_registry_gen.h", content: formatCpp(generateCapabilitiesH(capabilities, capabilitiesDigest)) },
  ];

  if (checkMode) {
    let hasChanges = false;
    for (const { path: relPath, content } of outputs) {
      const fullPath = path.join(repoRoot, relPath);
      if (!existsSync(fullPath)) {
        console.error(`MISSING: ${relPath}`);
        hasChanges = true;
        continue;
      }
      const existing = readFileSync(fullPath, "utf-8");
      if (existing !== content) {
        console.error(`OUT OF SYNC: ${relPath}`);
        hasChanges = true;
      }
    }
    if (hasChanges) {
      console.error("\nRun 'pnpm -C dsl gen' to regenerate.");
      process.exit(1);
    }
    console.log("All generated files are in sync.");
  } else {
    for (const { path: relPath, content } of outputs) {
      const fullPath = path.join(repoRoot, relPath);
      writeFileSync(fullPath, content);
      console.log(`Generated: ${relPath}`);
    }
    console.log("\nCodegen complete.");
  }
}

main();
