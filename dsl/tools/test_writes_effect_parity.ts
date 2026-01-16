/**
 * Parity test for writes_effect evaluator (TS vs C++).
 *
 * This script outputs test cases as JSON that the C++ side will consume
 * and compare results.
 *
 * Run with: tsx dsl/tools/test_writes_effect_parity.ts
 */

import {
  evalWrites,
  serializeWritesEffect,
  effectKeys,
  effectFromParam,
  effectSwitchEnum,
  effectUnion,
  type WritesEffectExpr,
  type EffectGamma,
} from "@ranking-dsl/runtime";

interface TestCase {
  name: string;
  expr: ReturnType<typeof serializeWritesEffect>;
  gamma: Record<string, number | string>;
  expected_kind: "exact" | "may" | "unknown";
  expected_keys: number[];
}

function gammaToRecord(gamma: EffectGamma): Record<string, number | string> {
  const result: Record<string, number | string> = {};
  for (const [k, v] of gamma) {
    result[k] = v;
  }
  return result;
}

function runTest(
  name: string,
  expr: WritesEffectExpr,
  gamma: EffectGamma
): TestCase {
  const result = evalWrites(expr, gamma);
  return {
    name,
    expr: serializeWritesEffect(expr),
    gamma: gammaToRecord(gamma),
    expected_kind: result.kind,
    expected_keys: result.kind === "unknown" ? [] : result.keys,
  };
}

// Generate test cases
const testCases: TestCase[] = [];

// EffectKeys tests
testCases.push(runTest("keys_empty", effectKeys([]), new Map()));
testCases.push(runTest("keys_single", effectKeys([1001]), new Map()));
testCases.push(runTest("keys_multiple_sorted", effectKeys([3, 1, 2]), new Map()));
testCases.push(runTest("keys_deduped", effectKeys([1, 2, 1, 3, 2]), new Map()));

// EffectFromParam tests
testCases.push(runTest("from_param_unknown", effectFromParam("out_key"), new Map()));
testCases.push(
  runTest(
    "from_param_known",
    effectFromParam("out_key"),
    new Map([["out_key", 1001]])
  )
);
testCases.push(
  runTest(
    "from_param_wrong_type",
    effectFromParam("out_key"),
    new Map([["out_key", "string_not_number"]])
  )
);

// EffectSwitchEnum tests
testCases.push(
  runTest(
    "switch_enum_esr",
    effectSwitchEnum("stage", {
      esr: effectKeys([4001]),
      lsr: effectKeys([4002]),
    }),
    new Map([["stage", "esr"]])
  )
);
testCases.push(
  runTest(
    "switch_enum_lsr",
    effectSwitchEnum("stage", {
      esr: effectKeys([4001]),
      lsr: effectKeys([4002]),
    }),
    new Map([["stage", "lsr"]])
  )
);
testCases.push(
  runTest(
    "switch_enum_unknown_param",
    effectSwitchEnum("stage", {
      esr: effectKeys([4001]),
      lsr: effectKeys([4002]),
    }),
    new Map()
  )
);
testCases.push(
  runTest(
    "switch_enum_missing_case",
    effectSwitchEnum("stage", {
      esr: effectKeys([4001]),
    }),
    new Map([["stage", "unknown_stage"]])
  )
);
testCases.push(
  runTest(
    "switch_enum_empty_cases",
    effectSwitchEnum("stage", {}),
    new Map()
  )
);

// EffectUnion tests
testCases.push(
  runTest(
    "union_exact",
    effectUnion([effectKeys([1, 2]), effectKeys([3, 4])]),
    new Map()
  )
);
testCases.push(
  runTest(
    "union_may",
    effectUnion([
      effectKeys([10]),
      effectSwitchEnum("param", {
        a: effectKeys([1]),
        b: effectKeys([2]),
      }),
    ]),
    new Map()
  )
);
testCases.push(
  runTest(
    "union_unknown",
    effectUnion([effectKeys([1]), effectFromParam("unknown_param")]),
    new Map()
  )
);
testCases.push(runTest("union_empty", effectUnion([]), new Map()));

// Nested tests
testCases.push(
  runTest(
    "nested_known",
    effectUnion([
      effectKeys([1]),
      effectSwitchEnum("inner", {
        x: effectKeys([100]),
        y: effectKeys([200]),
      }),
    ]),
    new Map([["inner", "x"]])
  )
);
testCases.push(
  runTest(
    "nested_unknown",
    effectUnion([
      effectKeys([1]),
      effectSwitchEnum("inner", {
        x: effectKeys([100]),
        y: effectKeys([200]),
      }),
    ]),
    new Map()
  )
);

// Output test cases as JSON
console.log(JSON.stringify({ test_cases: testCases }, null, 2));
