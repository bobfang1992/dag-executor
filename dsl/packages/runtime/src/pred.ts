/**
 * Predicate builder for filter tasks.
 * Produces PredIR matching engine's pred_table format.
 */

import type { KeyToken, ParamToken, ExprNode, PredNode, RegexPattern } from "@ranking-dsl/generated";
import { assertNotUndefined } from "./guards.js";

// Re-export types from generated for consumers
export type { PredNode, RegexPattern } from "@ranking-dsl/generated";

/**
 * Predicate builder API.
 */
export const Pred = {
  constBool(value: boolean): PredNode {
    assertNotUndefined(value, "Pred.constBool(value)");
    return { op: "const_bool", value };
  },

  and(a: PredNode, b: PredNode): PredNode {
    assertNotUndefined(a, "Pred.and(a, ...)");
    assertNotUndefined(b, "Pred.and(..., b)");
    return { op: "and", a, b };
  },

  or(a: PredNode, b: PredNode): PredNode {
    assertNotUndefined(a, "Pred.or(a, ...)");
    assertNotUndefined(b, "Pred.or(..., b)");
    return { op: "or", a, b };
  },

  not(a: PredNode): PredNode {
    assertNotUndefined(a, "Pred.not(a)");
    return { op: "not", x: a };
  },

  cmp(
    cmp: "==" | "!=" | "<" | "<=" | ">" | ">=",
    a: ExprNode,
    b: ExprNode
  ): PredNode {
    assertNotUndefined(cmp, "Pred.cmp(cmp, ...)");
    assertNotUndefined(a, "Pred.cmp(..., a, ...)");
    assertNotUndefined(b, "Pred.cmp(..., b)");
    return { op: "cmp", cmp, a, b };
  },

  in(lhs: ExprNode, list: (number | string)[]): PredNode {
    assertNotUndefined(lhs, "Pred.in(lhs, ...)");
    assertNotUndefined(list, "Pred.in(..., list)");
    if (!Array.isArray(list)) {
      throw new Error("Pred.in requires array of literal values");
    }
    // Empty list is allowed - engine treats it as "always false"
    if (list.length > 0) {
      const firstType = typeof list[0];
      if (firstType !== "number" && firstType !== "string") {
        throw new Error(`Pred.in list[0] must be number or string, got ${firstType}`);
      }
      for (let i = 0; i < list.length; i++) {
        const val = list[i];
        assertNotUndefined(val, `Pred.in(..., list[${i}])`);
        if (typeof val !== firstType) {
          throw new Error(`Pred.in list must be homogeneous: list[0] is ${firstType}, list[${i}] is ${typeof val}`);
        }
      }
    }
    return { op: "in", lhs, list };
  },

  isNull(a: ExprNode): PredNode {
    assertNotUndefined(a, "Pred.isNull(a)");
    return { op: "is_null", x: a };
  },

  notNull(a: ExprNode): PredNode {
    assertNotUndefined(a, "Pred.notNull(a)");
    return { op: "not_null", x: a };
  },

  /**
   * Regex predicate.
   * @param key - Key token to match against (must be string column)
   * @param pattern - Literal string or param token
   * @param flags - Regex flags: "" (default) or "i" (case-insensitive)
   */
  regex(
    key: KeyToken,
    pattern: string | ParamToken,
    flags: "" | "i" = ""
  ): PredNode {
    assertNotUndefined(key, "Pred.regex(key, ...)");
    assertNotUndefined(pattern, "Pred.regex(..., pattern, ...)");
    assertNotUndefined(flags, "Pred.regex(..., flags)");
    if (flags !== "" && flags !== "i") {
      throw new Error(`Pred.regex flags must be "" or "i", got "${flags}"`);
    }

    let regexPattern: RegexPattern;
    if (typeof pattern === "string") {
      regexPattern = { kind: "literal", value: pattern };
    } else {
      regexPattern = { kind: "param", param_id: pattern.id };
    }

    return {
      op: "regex",
      key_id: key.id,
      pattern: regexPattern,
      flags,
    };
  },
};

/**
 * Standalone regex function for natural predicate syntax.
 *
 * Usage in filter():
 *   c.filter({ pred: regex(Key.title, "^test") })
 *   c.filter({ pred: regex(Key.title, P.pattern, "i") })
 *
 * This function is recognized by the AST extractor and compiled to PredIR
 * at compile time. It should never be called at runtime for valid plans.
 * If called at runtime, it means the plan was not properly compiled.
 *
 * For builder-style usage, use Pred.regex() instead.
 */
export function regex(
  _key: unknown,
  _pattern: unknown,
  _flags?: unknown
): never {
  throw new Error(
    "regex() should only be used in filter() natural predicates. " +
    "The compiler extracts these at compile time. " +
    "For builder-style predicates, use Pred.regex() instead."
  );
}
