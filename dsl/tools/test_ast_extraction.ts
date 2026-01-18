#!/usr/bin/env tsx
/**
 * Tests for AST expression extraction genericity.
 * Proves the extractor works for:
 * 1. Any method with { expr: ... } property (not just vm)
 * 2. vm with variable outKey (not just direct Key.*)
 */

import { extractExpressions } from "../packages/compiler/src/ast-extractor.js";

let passed = 0;
let failed = 0;

function test(name: string, fn: () => void) {
  try {
    fn();
    console.log(`✓ ${name}`);
    passed++;
  } catch (e) {
    console.log(`✗ ${name}`);
    console.log(`  ${e instanceof Error ? e.message : e}`);
    failed++;
  }
}

function assertEqual<T>(actual: T, expected: T, msg: string) {
  if (actual !== expected) {
    throw new Error(`${msg}: expected ${expected}, got ${actual}`);
  }
}

// Test 1: Generic extraction - any method with { expr: ... }
test("extracts expr from arbitrary method (not just vm)", () => {
  const source = `
    import { definePlan, Key, P, coalesce } from "@ranking-dsl/runtime";
    export default definePlan({
      name: "test",
      build: (ctx) => {
        return ctx.viewer
          .follow({ fanout: 10 })
          .someFakeMethod({
            outKey: Key.final_score,
            expr: Key.id * coalesce(P.media_age_penalty_weight, 0.5),
          })
          .take({ count: 5 });
      },
    });
  `;
  const result = extractExpressions(source, "test.plan.ts");
  assertEqual(result.errors.length, 0, "errors");
  assertEqual(result.extractedExprs.size, 1, "extracted count");
  const expr = result.extractedExprs.get(0);
  assertEqual(expr?.op, "mul", "expr op");
});

// Test 2: vm with variable outKey (object form)
test("extracts expr from vm when outKey is a variable", () => {
  const source = `
    import { definePlan, Key, P } from "@ranking-dsl/runtime";
    export default definePlan({
      name: "test",
      build: (ctx) => {
        const outKey = Key.final_score;
        return ctx.viewer
          .follow({ fanout: 10 })
          .vm({ outKey, expr: Key.id * 2 })
          .take({ count: 5 });
      },
    });
  `;
  const result = extractExpressions(source, "test.plan.ts");
  assertEqual(result.errors.length, 0, "errors");
  assertEqual(result.extractedExprs.size, 1, "extracted count");
  const expr = result.extractedExprs.get(0);
  assertEqual(expr?.op, "mul", "expr op");
});

// Test 3: vm with direct Key.* in object form
test("extracts expr from vm with direct Key.* outKey", () => {
  const source = `
    import { definePlan, Key } from "@ranking-dsl/runtime";
    export default definePlan({
      name: "test",
      build: (ctx) => {
        return ctx.viewer
          .follow({ fanout: 10 })
          .vm({ outKey: Key.final_score, expr: Key.id + 100 })
          .take({ count: 5 });
      },
    });
  `;
  const result = extractExpressions(source, "test.plan.ts");
  assertEqual(result.errors.length, 0, "errors");
  assertEqual(result.extractedExprs.size, 1, "extracted count");
  const expr = result.extractedExprs.get(0);
  assertEqual(expr?.op, "add", "expr op");
});

// Test 4: Object form still works (generic { expr: ... } detection)
test("extracts expr from object form vm({ expr: ... })", () => {
  const source = `
    import { definePlan, Key, P } from "@ranking-dsl/runtime";
    export default definePlan({
      name: "test",
      build: (ctx) => {
        return ctx.viewer
          .follow({ fanout: 10 })
          .vm({ outKey: Key.final_score, expr: Key.id - P.esr_cutoff })
          .take({ count: 5 });
      },
    });
  `;
  const result = extractExpressions(source, "test.plan.ts");
  assertEqual(result.errors.length, 0, "errors");
  assertEqual(result.extractedExprs.size, 1, "extracted count");
  const expr = result.extractedExprs.get(0);
  assertEqual(expr?.op, "sub", "expr op");
});

// Test 5: Division is rejected
test("rejects division operator", () => {
  const source = `
    import { definePlan, Key } from "@ranking-dsl/runtime";
    export default definePlan({
      name: "test",
      build: (ctx) => {
        return ctx.viewer
          .follow({ fanout: 10 })
          .vm({ outKey: Key.final_score, expr: Key.id / 2 })
          .take({ count: 5 });
      },
    });
  `;
  const result = extractExpressions(source, "test.plan.ts");
  assertEqual(result.errors.length, 1, "errors");
  assertEqual(result.errors[0].message.includes("Division"), true, "error message");
});

// Test 6: Parenthesized builder-style is skipped (not extracted)
test("skips parenthesized builder-style expressions", () => {
  const source = `
    import { definePlan, Key, E } from "@ranking-dsl/runtime";
    export default definePlan({
      name: "test",
      build: (ctx) => {
        return ctx.viewer
          .follow({ fanout: 10 })
          .vm({ outKey: Key.final_score, expr: (E.mul(E.key(Key.id), E.const(2))) })
          .take({ count: 5 });
      },
    });
  `;
  const result = extractExpressions(source, "test.plan.ts");
  assertEqual(result.errors.length, 0, "errors");
  // Builder-style expressions should NOT be extracted
  assertEqual(result.extractedExprs.size, 0, "extracted count");
});

// Summary
console.log(`\n${passed} passed, ${failed} failed`);
process.exit(failed > 0 ? 1 : 0);
