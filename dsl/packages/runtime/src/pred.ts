/**
 * Predicate builder for filter tasks.
 * Produces PredIR matching engine's pred_table format.
 */

import type { KeyToken, ParamToken } from "@ranking-dsl/generated";
import { assertNotUndefined } from "./guards.js";
import type { ExprNode } from "./expr.js";

/**
 * PredNode - matches engine's PredIR format.
 */
export type PredNode =
  | { op: "const_bool"; value: boolean }
  | { op: "and"; a: PredNode; b: PredNode }
  | { op: "or"; a: PredNode; b: PredNode }
  | { op: "not"; a: PredNode }
  | { op: "cmp"; cmp: "==" | "!=" | "<" | "<=" | ">" | ">="; a: ExprNode; b: ExprNode }
  | { op: "in"; key: ExprNode; values: ExprNode[] }
  | { op: "is_null"; a: ExprNode }
  | { op: "not_null"; a: ExprNode }
  | { op: "regex"; key_id: number; pattern: RegexPattern; flags: string };

/**
 * Regex pattern - can be a literal string or a param reference.
 */
export type RegexPattern =
  | { kind: "literal"; value: string }
  | { kind: "param"; param_id: number };

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
    return { op: "not", a };
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

  in(key: ExprNode, values: ExprNode[]): PredNode {
    assertNotUndefined(key, "Pred.in(key, ...)");
    assertNotUndefined(values, "Pred.in(..., values)");
    if (!Array.isArray(values)) {
      throw new Error("Pred.in requires array of values");
    }
    for (let i = 0; i < values.length; i++) {
      assertNotUndefined(values[i], `Pred.in(..., values[${i}])`);
    }
    return { op: "in", key, values };
  },

  isNull(a: ExprNode): PredNode {
    assertNotUndefined(a, "Pred.isNull(a)");
    return { op: "is_null", a };
  },

  notNull(a: ExprNode): PredNode {
    assertNotUndefined(a, "Pred.notNull(a)");
    return { op: "not_null", a };
  },

  /**
   * Regex predicate.
   * @param key - Key token to match against
   * @param pattern - Literal string or param token
   * @param flags - Regex flags (e.g., "i" for case-insensitive)
   */
  regex(
    key: KeyToken,
    pattern: string | ParamToken,
    flags: string = ""
  ): PredNode {
    assertNotUndefined(key, "Pred.regex(key, ...)");
    assertNotUndefined(pattern, "Pred.regex(..., pattern, ...)");
    assertNotUndefined(flags, "Pred.regex(..., flags)");

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
