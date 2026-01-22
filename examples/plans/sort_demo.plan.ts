/**
 * Example plan: sort_demo
 *
 * Pipeline:
 * 1) viewer (current user)
 * 2) viewer.follow (fanout=7)
 * 3) vm -> final_score = id * 0.1
 * 4) sort by final_score desc (stable, nulls last)
 * 5) take top 5
 *
 * Expected ids after sort+take: [7,6,5,4,3]
 */

import { definePlan } from "@ranking-dsl/runtime";

export default definePlan({
  name: "sort_demo",
  build: (ctx) => {
    const source = ctx.viewer({ endpoint: EP.redis.redis_default })
      .follow({ endpoint: EP.redis.redis_default, fanout: 7, trace: "src" });
    const scored = source.vm({
      outKey: Key.final_score,
      expr: Key.id * 0.1,
      trace: "score",
    });
    const sorted = scored.sort({ by: Key.final_score, order: "desc", trace: "sort" });
    return sorted.take({ count: 5, trace: "take" });
  },
});
