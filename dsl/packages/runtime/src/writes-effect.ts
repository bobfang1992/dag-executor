/**
 * RFC0005: writes_effect expression language.
 *
 * This module provides types and an evaluator for expressing param-dependent
 * write effects in TaskSpec. The evaluator returns Exact(K) | May(K) | Unknown.
 */

// -----------------------------------------------------------------------------
// Effect Expression ADT
// -----------------------------------------------------------------------------

/** Keys{keyIds} -> always Exact({keys}) */
export interface EffectKeys {
  kind: "Keys";
  keyIds: number[];
}

/** FromParam(param) -> Exact if param constant, else Unknown */
export interface EffectFromParam {
  kind: "FromParam";
  param: string;
}

/** SwitchEnum(param, cases) -> Exact if param constant, May if bounded, else Unknown */
export interface EffectSwitchEnum {
  kind: "SwitchEnum";
  param: string;
  cases: Record<string, WritesEffectExpr>;
}

/** Union([e1, e2, ...]) -> combines effects */
export interface EffectUnion {
  kind: "Union";
  items: WritesEffectExpr[];
}

export type WritesEffectExpr =
  | EffectKeys
  | EffectFromParam
  | EffectSwitchEnum
  | EffectUnion;

// -----------------------------------------------------------------------------
// Evaluation Result
// -----------------------------------------------------------------------------

export interface ExactEffect {
  kind: "exact";
  keys: number[];
}

export interface MayEffect {
  kind: "may";
  keys: number[];
}

export interface UnknownEffect {
  kind: "unknown";
}

export type WritesEffect = ExactEffect | MayEffect | UnknownEffect;

// -----------------------------------------------------------------------------
// Gamma: compile/link-time environment
// -----------------------------------------------------------------------------

/** Maps param name to concrete value (number for key_id, string for enum) */
export type EffectGamma = Map<string, number | string>;

// -----------------------------------------------------------------------------
// Evaluator
// -----------------------------------------------------------------------------

/** Merge and sort key arrays, removing duplicates */
function mergeKeys(a: number[], b: number[]): number[] {
  const set = new Set([...a, ...b]);
  return [...set].sort((x, y) => x - y);
}

/** Combine two WritesEffect results */
function combineEffects(a: WritesEffect, b: WritesEffect): WritesEffect {
  // any Unknown => Unknown
  if (a.kind === "unknown" || b.kind === "unknown") {
    return { kind: "unknown" };
  }

  // Merge keys
  const merged = mergeKeys(a.keys, b.keys);

  // all Exact => Exact, else May
  if (a.kind === "exact" && b.kind === "exact") {
    return { kind: "exact", keys: merged };
  }

  return { kind: "may", keys: merged };
}

/**
 * Evaluate a writes_effect expression with given gamma context.
 * Returns Exact(keys), May(keys), or Unknown.
 */
export function evalWrites(
  expr: WritesEffectExpr,
  gamma: EffectGamma
): WritesEffect {
  switch (expr.kind) {
    case "Keys": {
      // Keys{} => always Exact with sorted, deduped keys (set semantics)
      const deduped = [...new Set(expr.keyIds)].sort((a, b) => a - b);
      return { kind: "exact", keys: deduped };
    }

    case "FromParam": {
      const val = gamma.get(expr.param);
      if (typeof val === "number") {
        // Param is known at compile/link time as a key_id
        return { kind: "exact", keys: [val] };
      }
      // Param not known => Unknown
      return { kind: "unknown" };
    }

    case "SwitchEnum": {
      const val = gamma.get(expr.param);
      if (typeof val === "string") {
        // Param is known as a string enum value
        const caseExpr = expr.cases[val];
        if (caseExpr) {
          return evalWrites(caseExpr, gamma);
        }
        // Known value but no matching case => Unknown
        return { kind: "unknown" };
      }

      // Param not constant => compute May(union of all cases)
      // If all cases are Exact or May, result is May(union), else Unknown
      const caseNames = Object.keys(expr.cases);
      if (caseNames.length === 0) {
        return { kind: "exact", keys: [] };
      }

      let allBounded = true;
      const allKeys = new Set<number>();

      for (const caseName of caseNames) {
        const caseExpr = expr.cases[caseName];
        const caseResult = evalWrites(caseExpr, gamma);
        if (caseResult.kind === "unknown") {
          allBounded = false;
          break;
        }
        for (const k of caseResult.keys) {
          allKeys.add(k);
        }
      }

      if (allBounded) {
        return { kind: "may", keys: [...allKeys].sort((a, b) => a - b) };
      }
      return { kind: "unknown" };
    }

    case "Union": {
      if (expr.items.length === 0) {
        return { kind: "exact", keys: [] };
      }

      let result: WritesEffect = { kind: "exact", keys: [] };
      for (const item of expr.items) {
        const itemResult = evalWrites(item, gamma);
        result = combineEffects(result, itemResult);
        if (result.kind === "unknown") {
          break; // Short-circuit on Unknown
        }
      }
      return result;
    }
  }
}

// -----------------------------------------------------------------------------
// Serialization (for parity testing)
// -----------------------------------------------------------------------------

/**
 * Serialize writes_effect to JSON object for parity testing.
 * Uses deterministic key ordering.
 */
export function serializeWritesEffect(
  expr: WritesEffectExpr
): Record<string, unknown> {
  switch (expr.kind) {
    case "Keys": {
      // Sort and dedupe for canonical output (set semantics)
      const deduped = [...new Set(expr.keyIds)].sort((a, b) => a - b);
      return { kind: "Keys", key_ids: deduped };
    }

    case "FromParam":
      return { kind: "FromParam", param: expr.param };

    case "SwitchEnum": {
      // Sort case keys for deterministic output
      const caseKeys = Object.keys(expr.cases).sort();
      const cases: Record<string, unknown> = {};
      for (const k of caseKeys) {
        cases[k] = serializeWritesEffect(expr.cases[k]);
      }
      return { kind: "SwitchEnum", param: expr.param, cases };
    }

    case "Union": {
      const items = expr.items.map((item) => serializeWritesEffect(item));
      return { kind: "Union", items };
    }
  }
}

// -----------------------------------------------------------------------------
// Builder helpers (for DSL use)
// -----------------------------------------------------------------------------

export function effectKeys(keyIds: number[]): EffectKeys {
  return { kind: "Keys", keyIds };
}

export function effectFromParam(param: string): EffectFromParam {
  return { kind: "FromParam", param };
}

export function effectSwitchEnum(
  param: string,
  cases: Record<string, WritesEffectExpr>
): EffectSwitchEnum {
  return { kind: "SwitchEnum", param, cases };
}

export function effectUnion(items: WritesEffectExpr[]): EffectUnion {
  return { kind: "Union", items };
}
