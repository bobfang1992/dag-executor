/**
 * Test plan with mismatched name - should be rejected by compiler.
 * Filename is "name_mismatch.plan.ts" but plan_name is "wrong_name".
 */
import { definePlan, EP } from "@ranking-dsl/runtime";
// Key, P, coalesce are globals injected by the compiler

export default definePlan({
  name: "wrong_name", // Intentionally wrong - should match "name_mismatch"
  build: (ctx) => {
    const candidates = ctx.follow({ endpoint: EP.redis.default, fanout: 10 });
    return candidates.take({ count: 5, outputKeys: [Key.id] });
  },
});
