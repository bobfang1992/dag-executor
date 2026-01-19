/**
 * ESLint rule: inline-expr-only
 *
 * Requires that `expr` properties in task calls contain inline expressions,
 * not variable references. The AST extractor only processes inline expressions.
 *
 * BAD:  const myExpr = Key.id * 2; c.vm({ expr: myExpr })
 * BAD:  const expr = Key.id * 2; c.vm({ expr })  // shorthand
 * GOOD: c.vm({ expr: Key.id * 2 })
 */

import type { Rule } from "eslint";

const rule: Rule.RuleModule = {
  meta: {
    type: "problem",
    docs: {
      description: "Require inline expressions in task calls, not variable references",
      recommended: true,
    },
    messages: {
      noVariableExpr:
        "The 'expr' property must be an inline expression, not a variable reference. The AST extractor only processes inline expressions. Use builder-style (E.mul(...)) for variable-based expressions.",
      noShorthandExpr:
        "The 'expr' property must be an inline expression, not shorthand. The AST extractor only processes inline expressions. Write 'expr: <expression>' instead of just 'expr'.",
    },
    schema: [],
  },
  create(context) {
    return {
      Property(node) {
        // Check if this is an 'expr' property
        if (node.key.type !== "Identifier" || node.key.name !== "expr") {
          return;
        }

        // Check for shorthand: { expr } instead of { expr: value }
        if (node.shorthand) {
          context.report({
            node,
            messageId: "noShorthandExpr",
          });
          return;
        }

        // Check if value is a simple identifier (variable reference)
        if (node.value.type === "Identifier") {
          context.report({
            node: node.value,
            messageId: "noVariableExpr",
          });
        }
      },
    };
  },
};

export default rule;
