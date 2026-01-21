/**
 * ESLint rule: no-dsl-import-alias
 *
 * Disallows importing Key, P, EP, or coalesce from @ranking-dsl packages.
 * These are injected as globals by the compiler and should not be imported.
 *
 * BAD:  import { Key } from "@ranking-dsl/runtime"
 * BAD:  import { Key as K } from "@ranking-dsl/generated"
 * BAD:  import { EP } from "@ranking-dsl/generated"
 * GOOD: (no import) - Key, P, EP, coalesce are globals
 */

import type { Rule } from "eslint";

const RESTRICTED_IDENTIFIERS = ["Key", "P", "EP", "coalesce"];
const DSL_PACKAGES = ["@ranking-dsl/runtime", "@ranking-dsl/generated"];

const rule: Rule.RuleModule = {
  meta: {
    type: "problem",
    docs: {
      description: "Disallow importing Key, P, EP, or coalesce from @ranking-dsl packages (they are globals)",
      recommended: true,
    },
    messages: {
      noImport:
        "'{{name}}' is a global provided by the compiler. Do not import it. Remove '{{name}}' from your import statement.",
    },
    schema: [],
  },
  create(context) {
    return {
      ImportDeclaration(node) {
        // Only check @ranking-dsl package imports
        const source = String(node.source.value);
        if (!DSL_PACKAGES.some((pkg) => source === pkg || source.startsWith(pkg + "/"))) {
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

          // Report any import of restricted identifiers (not just aliases)
          if (RESTRICTED_IDENTIFIERS.includes(importedName)) {
            context.report({
              node: specifier,
              messageId: "noImport",
              data: {
                name: importedName,
              },
            });
          }
        }
      },
    };
  },
};

export default rule;
