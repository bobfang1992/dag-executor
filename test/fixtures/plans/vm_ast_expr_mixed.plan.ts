/**
 * Test fixture: mixed expression styles in same plan
 *
 * Uses both:
 * 1. Natural expression syntax: Key.id * coalesce(P.x, 0.2)
 * 2. Builder-style syntax: E.mul(E.key(Key.id), E.const(2))
 *
 * Expected: Both expressions end up in expr_table with different IDs.
 */

import { definePlan, E } from "@ranking-dsl/runtime";
// Key, P, coalesce are globals injected by the compiler

export default definePlan({
  name: "vm_ast_expr_mixed",
  build: (ctx) => {
    return ctx.viewer({ endpoint: EP.redis.default })
      .follow({ endpoint: EP.redis.default, fanout: 10, trace: "src" })
      // First vm: natural expression syntax (object form)
      .vm({
        outKey: Key.final_score,
        expr: Key.id * coalesce(P.media_age_penalty_weight, 0.2),
        trace: "vm_natural",
      })
      // Second vm: builder-style (object form)
      .vm({
        outKey: Key.model_score_1,
        expr: E.mul(E.key(Key.id), E.const(2)),
        trace: "vm_builder",
      })
      .take({ count: 5, trace: "take" });
  },
});
