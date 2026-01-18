/**
 * Test fixture: division operator rejection
 *
 * Uses division (/) in natural expression - this should fail compilation
 * with a clear error message.
 */

import { definePlan, Key, P } from "@ranking-dsl/runtime";

export default definePlan({
  name: "vm_ast_expr_div",
  build: (ctx) => {
    return ctx.viewer
      .follow({ fanout: 10, trace: "src" })
      .vm({
        outKey: Key.final_score,
        expr: Key.id / P.media_age_penalty_weight,  // Division is not allowed
        trace: "vm_div",
      })
      .take({ count: 5, trace: "take" });
  },
});
