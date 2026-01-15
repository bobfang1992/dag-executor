/**
 * Example plan: regex_plan
 *
 * Pipeline:
 * 1. viewer.follow fanout=10
 * 2. filter regex(country, "US")
 * 3. take 5
 *
 * Expected results:
 * - ids: [1,3,5,7,9] (odd ids have country="US")
 */

import { definePlan, Pred, Key } from "@ranking-dsl/runtime";

export default definePlan({
  name: "regex_plan",
  build: (ctx) => {
    return ctx.viewer
      .follow({ fanout: 10, trace: "src" })
      .filter({
        pred: Pred.regex(Key.country, "US"),
        trace: "regex_filter",
      })
      .take({ count: 5, trace: "take" });
  },
});
