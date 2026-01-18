/**
 * Test fixture: natural expression syntax in vm()
 *
 * Uses the 2-arg form with natural TS expression:
 *   c.vm(Key.final_score, Key.id * coalesce(P.media_age_penalty_weight, 0.2))
 *
 * Expected: AST extractor compiles the expression to ExprIR and merges into expr_table.
 */

import { definePlan, Key, P, coalesce } from "@ranking-dsl/runtime";

export default definePlan({
  name: "vm_ast_expr_basic",
  build: (ctx) => {
    return ctx.viewer
      .follow({ fanout: 10, trace: "src" })
      .vm(
        Key.final_score,
        Key.id * coalesce(P.media_age_penalty_weight, 0.2),
        { trace: "vm_natural" }
      )
      .take({ count: 5, trace: "take" });
  },
});
