/**
 * Test fixture: Uses aliased Key import (should fail ESLint).
 */
import { definePlan, Pred } from "@ranking-dsl/runtime";
import { Key as JK, P } from "@ranking-dsl/generated";

export default definePlan({
  name: "bad_alias",
  build: (ctx) => {
    const c = ctx.viewer.follow({ fanout: 10 });
    return c.filter({
      pred: Pred.regex(JK.country, "US"),
      trace: "filter_us",
    });
  },
});
