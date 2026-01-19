/**
 * Globals injected by esbuild's `inject` option.
 *
 * This makes Key, P, and coalesce available as globals in all plan files
 * without requiring explicit imports. The AST extractor recognizes these
 * exact identifiers for natural expression extraction.
 *
 * Usage in plans (no import needed):
 *   c.vm({ outKey: Key.final_score, expr: Key.id * coalesce(P.weight, 0.2) })
 */

export { Key, P } from "@ranking-dsl/generated";
export { coalesce } from "@ranking-dsl/runtime";
