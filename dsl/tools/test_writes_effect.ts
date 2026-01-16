/**
 * Test script for writes_effect evaluator.
 * Run with: tsx dsl/tools/test_writes_effect.ts
 */

import {
  evalWrites,
  serializeWritesEffect,
  effectKeys,
  effectFromParam,
  effectSwitchEnum,
  effectUnion,
  type WritesEffectExpr,
  type WritesEffect,
  type EffectGamma,
} from "@ranking-dsl/runtime";

// Simple test runner
let testCount = 0;
let passCount = 0;

function test(name: string, fn: () => void): void {
  testCount++;
  try {
    fn();
    passCount++;
    console.log(`✓ ${name}`);
  } catch (e) {
    console.log(`✗ ${name}`);
    console.log(`  ${e}`);
  }
}

function assertEqual<T>(actual: T, expected: T, msg?: string): void {
  const actualStr = JSON.stringify(actual);
  const expectedStr = JSON.stringify(expected);
  if (actualStr !== expectedStr) {
    throw new Error(
      `${msg || "Assertion failed"}: expected ${expectedStr}, got ${actualStr}`
    );
  }
}

function assertEffectEqual(actual: WritesEffect, expected: WritesEffect): void {
  assertEqual(actual.kind, expected.kind, "Effect kind");
  if (actual.kind !== "unknown" && expected.kind !== "unknown") {
    assertEqual(actual.keys, expected.keys, "Effect keys");
  }
}

// =============================================================================
// Tests
// =============================================================================

console.log("=== EffectKeys tests ===");

test("Empty keys returns Exact empty", () => {
  const expr: WritesEffectExpr = effectKeys([]);
  const result = evalWrites(expr, new Map());
  assertEffectEqual(result, { kind: "exact", keys: [] });
});

test("Single key returns Exact", () => {
  const expr = effectKeys([1001]);
  const result = evalWrites(expr, new Map());
  assertEffectEqual(result, { kind: "exact", keys: [1001] });
});

test("Multiple keys sorted", () => {
  const expr = effectKeys([3, 1, 2]);
  const result = evalWrites(expr, new Map());
  assertEffectEqual(result, { kind: "exact", keys: [1, 2, 3] });
});

test("Duplicate keys deduped (set semantics)", () => {
  const expr = effectKeys([1, 2, 1, 3, 2]);
  const result = evalWrites(expr, new Map());
  assertEffectEqual(result, { kind: "exact", keys: [1, 2, 3] });
});

console.log("\n=== EffectFromParam tests ===");

test("FromParam with empty gamma returns Unknown", () => {
  const expr = effectFromParam("out_key");
  const result = evalWrites(expr, new Map());
  assertEffectEqual(result, { kind: "unknown" });
});

test("FromParam with gamma returns Exact", () => {
  const expr = effectFromParam("out_key");
  const gamma: EffectGamma = new Map([["out_key", 1001]]);
  const result = evalWrites(expr, gamma);
  assertEffectEqual(result, { kind: "exact", keys: [1001] });
});

test("FromParam with wrong type in gamma returns Unknown", () => {
  const expr = effectFromParam("out_key");
  const gamma: EffectGamma = new Map([["out_key", "not_a_key_id"]]);
  const result = evalWrites(expr, gamma);
  assertEffectEqual(result, { kind: "unknown" });
});

console.log("\n=== EffectSwitchEnum tests ===");

test("SwitchEnum with matching case", () => {
  const expr = effectSwitchEnum("stage", {
    esr: effectKeys([4001]),
    lsr: effectKeys([4002]),
  });
  const gamma: EffectGamma = new Map([["stage", "esr"]]);
  const result = evalWrites(expr, gamma);
  assertEffectEqual(result, { kind: "exact", keys: [4001] });
});

test("SwitchEnum with different case", () => {
  const expr = effectSwitchEnum("stage", {
    esr: effectKeys([4001]),
    lsr: effectKeys([4002]),
  });
  const gamma: EffectGamma = new Map([["stage", "lsr"]]);
  const result = evalWrites(expr, gamma);
  assertEffectEqual(result, { kind: "exact", keys: [4002] });
});

test("SwitchEnum with unknown param returns May(union)", () => {
  const expr = effectSwitchEnum("stage", {
    esr: effectKeys([4001]),
    lsr: effectKeys([4002]),
  });
  const gamma: EffectGamma = new Map(); // No "stage" in gamma
  const result = evalWrites(expr, gamma);
  assertEffectEqual(result, { kind: "may", keys: [4001, 4002] });
});

test("SwitchEnum with missing case returns Unknown", () => {
  const expr = effectSwitchEnum("stage", {
    esr: effectKeys([4001]),
  });
  const gamma: EffectGamma = new Map([["stage", "unknown_stage"]]);
  const result = evalWrites(expr, gamma);
  assertEffectEqual(result, { kind: "unknown" });
});

console.log("\n=== EffectUnion tests ===");

test("Union combines Exact results to Exact", () => {
  const expr = effectUnion([effectKeys([1, 2]), effectKeys([3, 4])]);
  const result = evalWrites(expr, new Map());
  assertEffectEqual(result, { kind: "exact", keys: [1, 2, 3, 4] });
});

test("Union with May results in May", () => {
  const expr = effectUnion([
    effectKeys([10]),
    effectSwitchEnum("param", {
      a: effectKeys([1]),
      b: effectKeys([2]),
    }),
  ]);
  const gamma: EffectGamma = new Map(); // param not in gamma => May
  const result = evalWrites(expr, gamma);
  assertEffectEqual(result, { kind: "may", keys: [1, 2, 10] });
});

test("Union with Unknown results in Unknown", () => {
  const expr = effectUnion([effectKeys([1]), effectFromParam("unknown_param")]);
  const result = evalWrites(expr, new Map());
  assertEffectEqual(result, { kind: "unknown" });
});

test("Empty Union returns Exact empty", () => {
  const expr = effectUnion([]);
  const result = evalWrites(expr, new Map());
  assertEffectEqual(result, { kind: "exact", keys: [] });
});

console.log("\n=== Serialization tests ===");

test("Serialize EffectKeys", () => {
  const expr = effectKeys([3, 1, 2]);
  const json = serializeWritesEffect(expr);
  assertEqual(json.kind, "Keys");
  assertEqual(json.key_ids, [1, 2, 3]); // sorted
});

test("Serialize EffectFromParam", () => {
  const expr = effectFromParam("out_key");
  const json = serializeWritesEffect(expr);
  assertEqual(json.kind, "FromParam");
  assertEqual(json.param, "out_key");
});

test("Serialize EffectSwitchEnum", () => {
  const expr = effectSwitchEnum("stage", {
    esr: effectKeys([4001]),
    lsr: effectKeys([4002]),
  });
  const json = serializeWritesEffect(expr);
  assertEqual(json.kind, "SwitchEnum");
  assertEqual(json.param, "stage");
  assertEqual(
    (json.cases as Record<string, unknown>)["esr"],
    { kind: "Keys", key_ids: [4001] }
  );
  assertEqual(
    (json.cases as Record<string, unknown>)["lsr"],
    { kind: "Keys", key_ids: [4002] }
  );
});

test("Serialize EffectUnion", () => {
  const expr = effectUnion([effectKeys([1]), effectFromParam("p")]);
  const json = serializeWritesEffect(expr);
  assertEqual(json.kind, "Union");
  const items = json.items as Array<Record<string, unknown>>;
  assertEqual(items[0].kind, "Keys");
  assertEqual(items[1].kind, "FromParam");
});

console.log("\n=== Nested tests ===");

test("Nested SwitchEnum in Union with known inner param", () => {
  const expr = effectUnion([
    effectKeys([1]),
    effectSwitchEnum("inner", {
      x: effectKeys([100]),
      y: effectKeys([200]),
    }),
  ]);
  const gamma: EffectGamma = new Map([["inner", "x"]]);
  const result = evalWrites(expr, gamma);
  assertEffectEqual(result, { kind: "exact", keys: [1, 100] });
});

test("Nested SwitchEnum in Union with unknown inner param", () => {
  const expr = effectUnion([
    effectKeys([1]),
    effectSwitchEnum("inner", {
      x: effectKeys([100]),
      y: effectKeys([200]),
    }),
  ]);
  const gamma: EffectGamma = new Map();
  const result = evalWrites(expr, gamma);
  assertEffectEqual(result, { kind: "may", keys: [1, 100, 200] });
});

// =============================================================================
// Summary
// =============================================================================

console.log("\n" + "=".repeat(50));
console.log(`Tests: ${passCount}/${testCount} passed`);
if (passCount === testCount) {
  console.log("All tests passed!");
  process.exit(0);
} else {
  console.log("Some tests failed!");
  process.exit(1);
}
