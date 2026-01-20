/**
 * Example plan: concat_plan
 *
 * Pipeline:
 * 1. a = viewer.follow fanout=4
 * 2. b = viewer.fetch_cached_recommendation fanout=4
 * 3. concat(a, b)
 * 4. take 8
 *
 * Expected results:
 * - ids: [1,2,3,4,1001,1002,1003,1004]
 * - country/title behavior per concat_demo plan
 */

import { definePlan } from "@ranking-dsl/runtime";

export default definePlan({
  name: "concat_plan",
  build: (ctx) => {
    const a = ctx.viewer.follow({ fanout: 4, trace: "L" });
    const b = ctx.viewer.fetch_cached_recommendation({ fanout: 4, trace: "R" });
    return a.concat({ rhs: b, trace: "C" }).take({ count: 8, trace: "T" });
  },
});
