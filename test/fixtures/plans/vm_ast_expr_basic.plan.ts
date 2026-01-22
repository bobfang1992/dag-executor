/**
 * Test fixture: natural expression syntax in vm()
 *
 * Uses object form with natural TS expression:
 *   c.vm({ outKey: Key.final_score, expr: Key.id * coalesce(P.x, 0.2) })
 *
 * Expected: AST extractor compiles the expression to ExprIR and merges into expr_table.
 */

import { definePlan } from "@ranking-dsl/runtime";
// Key, P, coalesce are globals injected by the compiler

export default definePlan({
  name: "vm_ast_expr_basic",
  build: (ctx) => {
    return ctx.viewer({ endpoint: EP.redis.redis_default })
      .follow({ endpoint: EP.redis.redis_default, fanout: 10, trace: "src" })
      .vm({
        outKey: Key.final_score,
        expr: Key.id * coalesce(P.media_age_penalty_weight, 0.2),
        trace: "vm_natural",
      })
      .take({ count: 5, trace: "take" });
  },
});
