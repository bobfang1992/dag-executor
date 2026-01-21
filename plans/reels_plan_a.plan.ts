/**
 * Example plan: reels_plan_a
 *
 * Pipeline:
 * 1. viewer.follow fanout=10
 * 2. vm final_score = id * coalesce(P.media_age_penalty_weight, 0.2)
 * 3. filter final_score >= 0.6
 * 4. vm passthrough (demonstrates natural expr with key-only reference)
 * 5. take 5
 *
 * Expected results:
 * - Without overrides: ids [3,4,5,6,7], final_score approx [0.6..1.4]
 * - With overrides media_age_penalty_weight=0.5: ids [2,3,4,5,6]
 */

import { definePlan, E, Pred } from "@ranking-dsl/runtime";
// Key, P, coalesce are globals injected by the compiler

export default definePlan({
  name: "reels_plan_a",
  build: (ctx) => {
    return ctx
      .follow({ endpoint: EP.redis.default, fanout: 10, trace: "src" })
      .vm({
        outKey: Key.final_score,
        expr: Key.id * coalesce(P.media_age_penalty_weight, 0.2),
        trace: "vm_final",
      })
      .filter({
        pred: Pred.cmp(">=", E.key(Key.final_score), E.const(0.6)),
        trace: "filter",
      })
      .vm({
        outKey: Key.final_score,
        expr: Key.final_score,
        trace: "vm_passthrough",
      })
      .take({ count: 5, trace: "take" });
  },
});

