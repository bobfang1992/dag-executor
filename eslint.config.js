/**
 * Root ESLint config for plan files.
 * Applies @ranking-dsl rules to *.plan.ts files.
 */
import tseslint from "typescript-eslint";
import rankingDslPlugin from "./dsl/packages/eslint-plugin/dist/index.js";

export default [
  // Rules for plan files
  {
    files: ["**/*.plan.ts"],
    plugins: {
      "@ranking-dsl": rankingDslPlugin,
    },
    languageOptions: {
      parser: tseslint.parser,
      ecmaVersion: 2020,
      sourceType: "module",
    },
    rules: {
      "@ranking-dsl/no-dsl-import-alias": "error",
      "@ranking-dsl/no-dsl-reassign": "error",
      "@ranking-dsl/inline-expr-only": "error",
      "@ranking-dsl/plan-restricted-imports": "error",
    },
  },
];
