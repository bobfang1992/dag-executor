#!/usr/bin/env tsx
/**
 * Simple tests for ESLint rules.
 */

import { Linter, Rule } from "eslint";
import plugin from "../../index.js";

// Build plugins object for flat config
// In flat config, plugins map to objects with a `rules` property
const plugins = {
  "@ranking-dsl": {
    rules: plugin.rules as Record<string, Rule.RuleModule>,
  },
};

const linter = new Linter();

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

function assertHasError(code: string, ruleId: string, filename = "test.ts") {
  const messages = linter.verify(code, [
    {
      files: ["**/*.ts"],
      plugins,
      rules: { [ruleId]: "error" },
      languageOptions: { ecmaVersion: 2020, sourceType: "module" },
    },
  ], { filename });

  const hasError = messages.some((m) => m.ruleId === ruleId);
  if (!hasError) {
    throw new Error(`Expected error from ${ruleId}, got: ${JSON.stringify(messages)}`);
  }
}

function assertNoError(code: string, ruleId: string, filename = "test.ts") {
  const messages = linter.verify(code, [
    {
      files: ["**/*.ts"],
      plugins,
      rules: { [ruleId]: "error" },
      languageOptions: { ecmaVersion: 2020, sourceType: "module" },
    },
  ], { filename });

  const errors = messages.filter((m) => m.ruleId === ruleId);
  if (errors.length > 0) {
    throw new Error(`Expected no error from ${ruleId}, got: ${JSON.stringify(errors)}`);
  }
}

// Tests for no-dsl-import-alias (now bans all Key/P/coalesce imports since they are globals)
test("no-dsl-import-alias: rejects Key import (globals only)", () => {
  assertHasError(
    `import { Key } from "@ranking-dsl/runtime";`,
    "@ranking-dsl/no-dsl-import-alias"
  );
});

test("no-dsl-import-alias: rejects aliased Key", () => {
  assertHasError(
    `import { Key as K } from "@ranking-dsl/runtime";`,
    "@ranking-dsl/no-dsl-import-alias"
  );
});

test("no-dsl-import-alias: rejects coalesce import (globals only)", () => {
  assertHasError(
    `import { coalesce } from "@ranking-dsl/runtime";`,
    "@ranking-dsl/no-dsl-import-alias"
  );
});

test("no-dsl-import-alias: rejects aliased coalesce", () => {
  assertHasError(
    `import { coalesce as coal } from "@ranking-dsl/runtime";`,
    "@ranking-dsl/no-dsl-import-alias"
  );
});

test("no-dsl-import-alias: allows aliased non-restricted identifiers", () => {
  assertNoError(
    `import { E as ExprBuilder } from "@ranking-dsl/runtime";`,
    "@ranking-dsl/no-dsl-import-alias"
  );
});

test("no-dsl-import-alias: rejects Key from @ranking-dsl/generated", () => {
  assertHasError(
    `import { Key } from "@ranking-dsl/generated";`,
    "@ranking-dsl/no-dsl-import-alias"
  );
});

test("no-dsl-import-alias: rejects aliased Key from @ranking-dsl/generated", () => {
  assertHasError(
    `import { Key as JK } from "@ranking-dsl/generated";`,
    "@ranking-dsl/no-dsl-import-alias"
  );
});

test("no-dsl-import-alias: rejects P from @ranking-dsl/generated", () => {
  assertHasError(
    `import { P } from "@ranking-dsl/generated";`,
    "@ranking-dsl/no-dsl-import-alias"
  );
});

test("no-dsl-import-alias: rejects aliased P from @ranking-dsl/generated", () => {
  assertHasError(
    `import { P as Params } from "@ranking-dsl/generated";`,
    "@ranking-dsl/no-dsl-import-alias"
  );
});

test("no-dsl-import-alias: allows other imports from @ranking-dsl/generated", () => {
  assertNoError(
    `import { KeyToken, ParamToken } from "@ranking-dsl/generated";`,
    "@ranking-dsl/no-dsl-import-alias"
  );
});

// Tests for no-dsl-reassign
test("no-dsl-reassign: rejects const JK = Key", () => {
  assertHasError(
    `const JK = Key;`,
    "@ranking-dsl/no-dsl-reassign"
  );
});

test("no-dsl-reassign: rejects let params = P", () => {
  assertHasError(
    `let params = P;`,
    "@ranking-dsl/no-dsl-reassign"
  );
});

test("no-dsl-reassign: rejects const coal = coalesce", () => {
  assertHasError(
    `const coal = coalesce;`,
    "@ranking-dsl/no-dsl-reassign"
  );
});

test("no-dsl-reassign: allows const x = someOtherThing", () => {
  assertNoError(
    `const x = someOtherThing;`,
    "@ranking-dsl/no-dsl-reassign"
  );
});

test("no-dsl-reassign: allows const score = Key.id (property access, not reassign)", () => {
  assertNoError(
    `const score = Key.id;`,
    "@ranking-dsl/no-dsl-reassign"
  );
});

// Tests for inline-expr-only
test("inline-expr-only: allows inline expression", () => {
  assertNoError(
    `const x = { expr: Key.id * 2 };`,
    "@ranking-dsl/inline-expr-only"
  );
});

test("inline-expr-only: rejects variable reference", () => {
  assertHasError(
    `const myExpr = 1; const x = { expr: myExpr };`,
    "@ranking-dsl/inline-expr-only"
  );
});

test("inline-expr-only: rejects shorthand", () => {
  assertHasError(
    `const expr = 1; const x = { expr };`,
    "@ranking-dsl/inline-expr-only"
  );
});

// Tests for plan-restricted-imports
test("plan-restricted-imports: allows @ranking-dsl/runtime in plans", () => {
  assertNoError(
    `import { Key } from "@ranking-dsl/runtime";`,
    "@ranking-dsl/plan-restricted-imports",
    "my.plan.ts"
  );
});

test("plan-restricted-imports: allows @ranking-dsl/generated in plans", () => {
  assertNoError(
    `import { Key } from "@ranking-dsl/generated";`,
    "@ranking-dsl/plan-restricted-imports",
    "my.plan.ts"
  );
});

test("plan-restricted-imports: allows fragment imports in plans", () => {
  assertNoError(
    `import { esr } from "./esr.fragment.ts";`,
    "@ranking-dsl/plan-restricted-imports",
    "my.plan.ts"
  );
});

test("plan-restricted-imports: rejects arbitrary imports in plans", () => {
  assertHasError(
    `import { helper } from "./helpers/scoring";`,
    "@ranking-dsl/plan-restricted-imports",
    "my.plan.ts"
  );
});

test("plan-restricted-imports: ignores non-plan files", () => {
  assertNoError(
    `import { helper } from "./helpers/scoring";`,
    "@ranking-dsl/plan-restricted-imports",
    "utils.ts"  // Not a .plan.ts file
  );
});

// Summary
console.log(`\n${passed} passed, ${failed} failed`);
process.exit(failed > 0 ? 1 : 0);
