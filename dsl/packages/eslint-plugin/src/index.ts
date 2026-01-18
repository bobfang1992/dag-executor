/**
 * @ranking-dsl/eslint-plugin
 *
 * ESLint rules for ranking DSL plans to catch common issues early.
 */

import noDslImportAlias from "./rules/no-dsl-import-alias.js";
import inlineExprOnly from "./rules/inline-expr-only.js";
import planRestrictedImports from "./rules/plan-restricted-imports.js";

const plugin = {
  meta: {
    name: "@ranking-dsl/eslint-plugin",
    version: "0.1.0",
  },
  rules: {
    "no-dsl-import-alias": noDslImportAlias,
    "inline-expr-only": inlineExprOnly,
    "plan-restricted-imports": planRestrictedImports,
  },
  configs: {
    recommended: {
      plugins: ["@ranking-dsl"],
      rules: {
        "@ranking-dsl/no-dsl-import-alias": "error",
        "@ranking-dsl/inline-expr-only": "error",
        "@ranking-dsl/plan-restricted-imports": "error",
      },
    },
  },
};

export default plugin;
export { plugin };
