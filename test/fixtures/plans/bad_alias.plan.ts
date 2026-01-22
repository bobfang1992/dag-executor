/**
 * Test fixture: Imports Key (should fail ESLint).
 *
 * Key is a global provided by the compiler. Importing it (even without alias)
 * should be flagged by the no-dsl-import-alias rule.
 */
import { definePlan, Pred } from "@ranking-dsl/runtime";
import { Key as JK } from "@ranking-dsl/generated";  // ESLint should reject this

export default definePlan({
  name: "bad_alias",
  build: (ctx) => {
    const c = ctx.viewer({ endpoint: EP.redis.redis_default })
      .follow({ endpoint: EP.redis.redis_default, fanout: 10 });
    return c.filter({
      pred: Pred.regex(JK.country, "US"),
      trace: "filter_us",
    });
  },
});
