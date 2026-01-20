/**
 * Example plan: sort_demo
 *
 * Pipeline:
 * 1) viewer.follow (fanout=7)
 * 2) vm -> final_score = id * 0.1
 * 3) sort by final_score desc (stable, nulls last)
 * 4) take top 5
 *
 * Expected ids after sort+take: [7,6,5,4,3]
 */

import { definePlan } from "@ranking-dsl/runtime";

export default definePlan({
  name: "sort_demo",
  build: (ctx) => {
    const source = ctx.viewer.follow({ fanout: 7, trace: "src" });
    const scored = source.vm({
      outKey: Key.final_score,
      expr: Key.id * 0.1,
      trace: "score",
    });
    const sorted = scored.sort({ by: Key.final_score, order: "desc", trace: "sort" });
    return sorted.take({ count: 5, trace: "take" });
  },
});
