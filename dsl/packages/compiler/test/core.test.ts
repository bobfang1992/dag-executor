/**
 * Tests for the core compiler module.
 *
 * Run with: npx tsx test/core.test.ts
 */

import * as esbuild from 'esbuild';
import * as path from 'path';
import { fileURLToPath } from 'url';
import { compilePlan } from '../src/core/index.js';

const __dirname = path.dirname(fileURLToPath(import.meta.url));
const RUNTIME_PATH = path.resolve(__dirname, '../../runtime/src/index.ts');
const GENERATED_PATH = path.resolve(__dirname, '../../generated/index.ts');

/**
 * Pre-bundle a package into a single file that can be used as a virtual module.
 * This is necessary because the runtime has internal imports (./expr.js, etc.)
 * that the virtual module plugin can't resolve.
 */
async function prebundlePackage(entryPoint: string, packageName: string): Promise<string> {
  const result = await esbuild.build({
    entryPoints: [entryPoint],
    bundle: true,
    format: 'esm',
    platform: 'neutral',
    target: 'es2020',
    write: false,
    // Mark peer dependencies as external (they'll be resolved by the main bundler)
    external: packageName === '@ranking-dsl/runtime' ? ['@ranking-dsl/generated'] : [],
  });

  if (!result.outputFiles || result.outputFiles.length !== 1) {
    throw new Error(`Failed to prebundle ${packageName}`);
  }

  return result.outputFiles[0].text;
}

// Pre-bundled sources (initialized in runTests)
let runtimeSource: string;
let generatedSource: string;

let passed = 0;
let failed = 0;

function test(name: string, fn: () => Promise<void> | void) {
  return async () => {
    try {
      await fn();
      console.log(`✓ ${name}`);
      passed++;
    } catch (err) {
      console.error(`✗ ${name}`);
      console.error(`  ${err instanceof Error ? err.message : err}`);
      failed++;
    }
  };
}

function assert(condition: boolean, message: string) {
  if (!condition) throw new Error(message);
}

function assertEqual<T>(actual: T, expected: T, message?: string) {
  if (actual !== expected) {
    throw new Error(message || `Expected ${expected}, got ${actual}`);
  }
}

// Test cases
const tests = [
  test('compiles a simple plan', async () => {
    const planSource = `
      import { definePlan } from '@ranking-dsl/runtime';
      import { Key } from '@ranking-dsl/generated';

      export default definePlan({
        name: 'test_plan',
        build: (ctx) => {
          const source = ctx.viewer.follow({ fanout: 100 });
          return source.take({ count: 10 });
        },
      });
    `;

    const result = await compilePlan({
      planSource,
      planFilename: 'test_plan.plan.ts',
      runtimeSource,
      generatedSource,
      esbuild,
      toolVersion: '0.1.0-test',
    });

    assert(result.success, `Compilation failed: ${!result.success && result.error}`);
    if (result.success) {
      assertEqual(result.artifact.schema_version, 1);
      assertEqual(result.artifact.plan_name, 'test_plan');
      assert(Array.isArray(result.artifact.nodes), 'nodes should be an array');
      assertEqual((result.artifact.nodes as unknown[]).length, 2); // follow + take
    }
  }),

  test('compiles a plan with vm expression', async () => {
    const planSource = `
      import { definePlan } from '@ranking-dsl/runtime';
      import { Key, P } from '@ranking-dsl/generated';

      export default definePlan({
        name: 'vm_plan',
        build: (ctx) => {
          const source = ctx.viewer.follow({ fanout: 50 });
          const scored = source.vm({
            outKey: Key.final_score,
            expr: Key.model_score_1 + Key.model_score_2 * P.media_age_penalty_weight,
          });
          return scored.take({ count: 5 });
        },
      });
    `;

    const result = await compilePlan({
      planSource,
      planFilename: 'vm_plan.plan.ts',
      runtimeSource,
      generatedSource,
      esbuild,
    });

    assert(result.success, `Compilation failed: ${!result.success && result.error}`);
    if (result.success) {
      assertEqual(result.artifact.plan_name, 'vm_plan');
      assertEqual((result.artifact.nodes as unknown[]).length, 3); // follow + vm + take
      assert(result.artifact.expr_table !== undefined, 'should have expr_table');
    }
  }),

  test('rejects plan with syntax error', async () => {
    const planSource = `
      import { definePlan } from '@ranking-dsl/runtime';

      export default definePlan({
        name: 'bad_plan',
        build: (ctx) => {
          const source = ctx.viewer.follow({ fanout: 100 )  // syntax error
          return source.take({ count: 10 });
        },
      });
    `;

    const result = await compilePlan({
      planSource,
      planFilename: 'bad_plan.plan.ts',
      runtimeSource,
      generatedSource,
      esbuild,
    });

    assert(!result.success, 'Should have failed');
    if (!result.success) {
      assertEqual(result.phase, 'bundle');
    }
  }),

  test('rejects plan that does not call definePlan', async () => {
    const planSource = `
      // Missing definePlan call
      console.log('hello');
    `;

    const result = await compilePlan({
      planSource,
      planFilename: 'no_emit.plan.ts',
      runtimeSource,
      generatedSource,
      esbuild,
    });

    assert(!result.success, 'Should have failed');
    if (!result.success) {
      assertEqual(result.phase, 'execute');
      assert(result.error.includes('did not emit'), `Error should mention no emit: ${result.error}`);
    }
  }),

  test('rejects plan with undefined values', async () => {
    const planSource = `
      import { definePlan } from '@ranking-dsl/runtime';

      export default definePlan({
        name: 'undefined_plan',
        build: (ctx) => {
          // Pass undefined fanout
          const source = ctx.viewer.follow({ fanout: undefined as any });
          return source.take({ count: 10 });
        },
      });
    `;

    const result = await compilePlan({
      planSource,
      planFilename: 'undefined_plan.plan.ts',
      runtimeSource,
      generatedSource,
      esbuild,
    });

    assert(!result.success, 'Should have failed');
    if (!result.success) {
      assertEqual(result.phase, 'execute');
    }
  }),

  test('includes built_by metadata', async () => {
    const planSource = `
      import { definePlan } from '@ranking-dsl/runtime';

      export default definePlan({
        name: 'metadata_plan',
        build: (ctx) => {
          return ctx.viewer.follow({ fanout: 10 }).take({ count: 5 });
        },
      });
    `;

    const result = await compilePlan({
      planSource,
      planFilename: 'metadata_plan.plan.ts',
      runtimeSource,
      generatedSource,
      esbuild,
      toolVersion: '1.2.3-test',
    });

    assert(result.success, `Compilation failed: ${!result.success && result.error}`);
    if (result.success) {
      const builtBy = result.artifact.built_by as Record<string, unknown>;
      assert(builtBy !== undefined, 'should have built_by');
      assertEqual(builtBy.backend, 'quickjs');
      assertEqual(builtBy.tool, 'dslc');
      assertEqual(builtBy.tool_version, '1.2.3-test');
      assert(typeof builtBy.bundle_digest === 'string', 'should have bundle_digest');
    }
  }),
];

// Run all tests
async function runTests() {
  console.log('Running core compiler tests...\n');

  // Pre-bundle runtime and generated packages
  // This is necessary because they have internal imports that the virtual module
  // plugin can't resolve (e.g., runtime imports ./expr.js, ./plan.js, etc.)
  console.log('Pre-bundling runtime and generated packages...');
  generatedSource = await prebundlePackage(GENERATED_PATH, '@ranking-dsl/generated');
  runtimeSource = await prebundlePackage(RUNTIME_PATH, '@ranking-dsl/runtime');
  console.log('Pre-bundling complete.\n');

  for (const t of tests) {
    await t();
  }

  console.log(`\n${passed} passed, ${failed} failed`);

  if (failed > 0) {
    process.exit(1);
  }
}

runTests();
