/**
 * Example plan: regex_plan
 *
 * Pipeline:
 * 1. viewer (current user)
 * 2. viewer.follow fanout=10
 * 3. filter regex(country, "US")
 * 4. take 5
 *
 * Expected results:
 * - ids: [1,3,5,7,9] (odd ids have country="US")
 */

import { definePlan, Pred } from "@ranking-dsl/runtime";
// Key, P, coalesce are globals injected by the compiler

export default definePlan({
  name: "regex_plan",
  build: (ctx) => {
    return ctx
      .viewer({ endpoint: EP.redis.redis_default })
      .follow({ endpoint: EP.redis.redis_default, fanout: 10, trace: "src" })
      .filter({
        pred: Pred.regex(Key.country, "US"),
        trace: "regex_filter",
      })
      .take({ count: 5, trace: "take" });
  },
});
