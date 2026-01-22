/**
 * Example plan: concat_plan
 *
 * Pipeline:
 * 1. v = viewer (current user)
 * 2. a = v.follow fanout=4
 * 3. b = v.recommendation fanout=4
 * 4. concat(a, b)
 * 5. take 8
 *
 * Expected results:
 * - ids: [1,2,3,4,1001,1002,1003,1004]
 * - country/title behavior per concat_demo plan
 */

import { definePlan } from "@ranking-dsl/runtime";

export default definePlan({
  name: "concat_plan",
  build: (ctx) => {
    const v = ctx.viewer({ endpoint: EP.redis.redis_default });
    const a = v.follow({ endpoint: EP.redis.redis_default, fanout: 4, trace: "L" });
    const b = v.recommendation({ endpoint: EP.redis.redis_default, fanout: 4, trace: "R" });
    return a.concat({ rhs: b, trace: "C" }).take({ count: 8, trace: "T" });
  },
});
