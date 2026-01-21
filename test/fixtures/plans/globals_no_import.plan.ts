/**
 * Test fixture: Uses Key/P as globals without importing.
 *
 * Key, P, and coalesce are injected as globals by the compiler.
 * This plan should compile successfully without importing them.
 */
import { definePlan, E, Pred } from "@ranking-dsl/runtime";

export default definePlan({
  name: "globals_no_import",
  build: (ctx) => {
    return ctx.viewer({ endpoint: EP.redis.default })
      .follow({ endpoint: EP.redis.default, fanout: 10, trace: "src" })
      // Natural expression using globals
      .vm({
        outKey: Key.final_score,
        expr: Key.id * coalesce(P.media_age_penalty_weight, 0.2),
        trace: "vm_natural",
      })
      // Filter using globals
      .filter({
        pred: Pred.cmp(">=", E.key(Key.final_score), E.const(0.6)),
        trace: "filter",
      })
      .take({ count: 5, trace: "take" });
  },
});
