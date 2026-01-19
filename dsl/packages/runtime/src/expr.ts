/**
 * Expression builder for vm tasks.
 * Produces ExprIR matching engine's expr_table format.
 */

import type { KeyToken, ParamToken } from "@ranking-dsl/generated";
import { assertNotUndefined } from "./guards.js";

/**
 * ExprNode - matches engine's ExprIR format.
 */
export type ExprNode =
  | { op: "const_number"; value: number }
  | { op: "const_null" }
  | { op: "key_ref"; key_id: number }
  | { op: "param_ref"; param_id: number }
  | { op: "add"; a: ExprNode; b: ExprNode }
  | { op: "sub"; a: ExprNode; b: ExprNode }
  | { op: "mul"; a: ExprNode; b: ExprNode }
  | { op: "neg"; x: ExprNode }
  | { op: "coalesce"; a: ExprNode; b: ExprNode };

/**
 * StaticExprToken - placeholder for AST-extracted expressions.
 * The compiler replaces natural expressions with { __expr_id: N } and
 * then post-processes to merge them into the expr_table.
 */
export interface StaticExprToken {
  __expr_id: number;
}

/**
 * Expression builder API.
 */
export const E = {
  const(value: number): ExprNode {
    assertNotUndefined(value, "E.const(value)");
    if (!Number.isFinite(value)) {
      throw new Error(`E.const requires finite number, got ${value}`);
    }
    return { op: "const_number", value };
  },

  constNull(): ExprNode {
    return { op: "const_null" };
  },

  key(token: KeyToken): ExprNode {
    assertNotUndefined(token, "E.key(token)");
    return { op: "key_ref", key_id: token.id };
  },

  param(token: ParamToken): ExprNode {
    assertNotUndefined(token, "E.param(token)");
    return { op: "param_ref", param_id: token.id };
  },

  add(a: ExprNode, b: ExprNode): ExprNode {
    assertNotUndefined(a, "E.add(a, ...)");
    assertNotUndefined(b, "E.add(..., b)");
    return { op: "add", a, b };
  },

  sub(a: ExprNode, b: ExprNode): ExprNode {
    assertNotUndefined(a, "E.sub(a, ...)");
    assertNotUndefined(b, "E.sub(..., b)");
    return { op: "sub", a, b };
  },

  mul(a: ExprNode, b: ExprNode): ExprNode {
    assertNotUndefined(a, "E.mul(a, ...)");
    assertNotUndefined(b, "E.mul(..., b)");
    return { op: "mul", a, b };
  },

  neg(a: ExprNode): ExprNode {
    assertNotUndefined(a, "E.neg(a)");
    return { op: "neg", x: a };
  },

  coalesce(a: ExprNode, b: ExprNode): ExprNode {
    assertNotUndefined(a, "E.coalesce(a, ...)");
    assertNotUndefined(b, "E.coalesce(..., b)");
    return { op: "coalesce", a, b };
  },
};

/**
 * Standalone coalesce function for natural expression syntax.
 *
 * Usage in vm():
 *   c.vm({ outKey: Key.final_score, expr: Key.id * coalesce(P.weight, 0.2) })
 *
 * This function is recognized by the AST extractor and compiled to ExprIR
 * at compile time. It should never be called at runtime for valid plans.
 * If called at runtime, it means the plan was not properly compiled.
 *
 * For builder-style usage, use E.coalesce() instead.
 */
export function coalesce(_a: unknown, _b: unknown): never {
  throw new Error(
    "coalesce() should only be used in vm() natural expressions. " +
    "The compiler extracts these at compile time. " +
    "For builder-style expressions, use E.coalesce() instead."
  );
}
