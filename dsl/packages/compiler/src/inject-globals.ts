/**
 * Globals injected by esbuild's `inject` option.
 *
 * This makes Key, P, coalesce, and regex available as globals in all plan files
 * without requiring explicit imports. The AST extractor recognizes these
 * exact identifiers for natural expression/predicate extraction.
 *
 * Usage in plans (no import needed):
 *   c.vm({ outKey: Key.final_score, expr: Key.id * coalesce(P.weight, 0.2) })
 *   c.filter({ pred: Key.score > 0.5 && regex(Key.title, "^test") })
 */

export { Key, P } from "@ranking-dsl/generated";
export { coalesce, regex } from "@ranking-dsl/runtime";
