/**
 * ESLint rule: no-dsl-import-alias
 *
 * Disallows aliasing Key, P, or coalesce imports from @ranking-dsl/runtime.
 * The AST extractor only recognizes exact identifiers.
 *
 * BAD:  import { Key as K } from "@ranking-dsl/runtime"
 * GOOD: import { Key } from "@ranking-dsl/runtime"
 */

import type { Rule } from "eslint";

const RESTRICTED_IDENTIFIERS = ["Key", "P", "coalesce"];

const rule: Rule.RuleModule = {
  meta: {
    type: "problem",
    docs: {
      description: "Disallow aliasing Key, P, or coalesce from @ranking-dsl/runtime",
      recommended: true,
    },
    messages: {
      noAlias:
        "Import '{{name}}' must not be aliased. The AST extractor only recognizes exact identifiers. Use 'import { {{name}} }' instead of 'import { {{name}} as {{alias}} }'.",
    },
    schema: [],
  },
  create(context) {
    return {
      ImportDeclaration(node) {
        // Only check @ranking-dsl/runtime imports
        if (node.source.value !== "@ranking-dsl/runtime") {
          return;
        }

        for (const specifier of node.specifiers) {
          if (specifier.type !== "ImportSpecifier") {
            continue;
          }

          const importedName =
            specifier.imported.type === "Identifier"
              ? specifier.imported.name
              : String(specifier.imported.value);

          const localName = specifier.local.name;

          // Check if this is a restricted identifier being aliased
          if (
            RESTRICTED_IDENTIFIERS.includes(importedName) &&
            importedName !== localName
          ) {
            context.report({
              node: specifier,
              messageId: "noAlias",
              data: {
                name: importedName,
                alias: localName,
              },
            });
          }
        }
      },
    };
  },
};

export default rule;
