/**
 * ESLint rule: no-dsl-reassign
 *
 * Disallows reassigning Key, P, or coalesce to another variable.
 * The AST extractor only recognizes exact identifiers.
 *
 * BAD:  const JK = Key;
 * BAD:  let params = P;
 * BAD:  const coal = coalesce;
 * GOOD: (use Key, P, coalesce directly)
 */

import type { Rule } from "eslint";

const PROTECTED_GLOBALS = ["Key", "P", "coalesce"];

const rule: Rule.RuleModule = {
  meta: {
    type: "problem",
    docs: {
      description: "Disallow reassigning Key, P, or coalesce to another variable",
      recommended: true,
    },
    messages: {
      noReassign:
        "Do not reassign '{{name}}' to '{{varName}}'. The AST extractor only recognizes exact identifiers. Use '{{name}}' directly.",
    },
    schema: [],
  },
  create(context) {
    return {
      VariableDeclarator(node) {
        // Check if init is an Identifier that matches a protected global
        if (
          node.init &&
          node.init.type === "Identifier" &&
          PROTECTED_GLOBALS.includes(node.init.name)
        ) {
          // Get the variable name being assigned to
          const varName =
            node.id.type === "Identifier" ? node.id.name : "(destructured)";

          context.report({
            node,
            messageId: "noReassign",
            data: {
              name: node.init.name,
              varName,
            },
          });
        }
      },
    };
  },
};

export default rule;
