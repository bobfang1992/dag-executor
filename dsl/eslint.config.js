import eslint from "@eslint/js";
import tseslint from "typescript-eslint";
import rankingDslPlugin from "./packages/eslint-plugin/dist/index.js";

export default tseslint.config(
  eslint.configs.recommended,
  ...tseslint.configs.recommended,
  {
    files: ["src/**/*.ts"],
    languageOptions: {
      parserOptions: {
        project: "./tsconfig.json",
      },
    },
    rules: {
      "@typescript-eslint/no-explicit-any": "error",
      "@typescript-eslint/no-unused-vars": ["error", { argsIgnorePattern: "^_" }],
    },
  },
  {
    files: ["*.js"],
    rules: {
      "no-unused-vars": ["error", { argsIgnorePattern: "^_" }],
    },
  },
  // Rules for plan files
  {
    files: ["**/*.plan.ts"],
    plugins: {
      "@ranking-dsl": rankingDslPlugin,
    },
    rules: {
      "@ranking-dsl/no-dsl-import-alias": "error",
      "@ranking-dsl/no-dsl-reassign": "error",
      "@ranking-dsl/inline-expr-only": "error",
      "@ranking-dsl/plan-restricted-imports": "error",
    },
  },
);
