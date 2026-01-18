/**
 * ESLint rule: plan-restricted-imports
 *
 * Plans may only import from:
 * - @ranking-dsl/runtime
 * - @ranking-dsl/generated
 * - *.fragment.ts files
 *
 * No arbitrary shared helpers allowed.
 */

import type { Rule } from "eslint";

const ALLOWED_PACKAGES = ["@ranking-dsl/runtime", "@ranking-dsl/generated"];

function isAllowedImport(importPath: string): boolean {
  // Allow @ranking-dsl packages
  if (ALLOWED_PACKAGES.some((pkg) => importPath === pkg || importPath.startsWith(pkg + "/"))) {
    return true;
  }

  // Allow fragment imports
  if (importPath.endsWith(".fragment") || importPath.endsWith(".fragment.ts")) {
    return true;
  }

  return false;
}

const rule: Rule.RuleModule = {
  meta: {
    type: "problem",
    docs: {
      description: "Restrict plan imports to @ranking-dsl packages and fragments only",
      recommended: true,
    },
    messages: {
      restrictedImport:
        "Import '{{source}}' is not allowed in plans. Plans may only import from: @ranking-dsl/runtime, @ranking-dsl/generated, and *.fragment.ts files.",
    },
    schema: [],
  },
  create(context) {
    // Only apply to .plan.ts files
    const filename = context.filename || context.getFilename();
    if (!filename.endsWith(".plan.ts")) {
      return {};
    }

    return {
      ImportDeclaration(node) {
        const source = node.source.value;
        if (typeof source !== "string") {
          return;
        }

        if (!isAllowedImport(source)) {
          context.report({
            node,
            messageId: "restrictedImport",
            data: { source },
          });
        }
      },
    };
  },
};

export default rule;
