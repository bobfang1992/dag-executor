/**
 * Evil plan - tests QuickJS sandbox security restrictions.
 *
 * This plan attempts to use eval() which should be disabled in the sandbox.
 * Expected: dslc should fail with non-zero exit code.
 */

import { definePlan } from "@ranking-dsl/runtime";

export default definePlan({
  name: "evil_plan",
  build: (ctx) => {
    // Try to use eval - this should fail in QuickJS sandbox
    // and cause the entire build to fail
    // @ts-expect-error - intentionally using eval
    eval("console.log('This should not work')");

    // This line should never be reached if eval fails properly
    return ctx.viewer({ endpoint: EP.redis.redis_default }).follow({ endpoint: EP.redis.redis_default, fanout: 1 });
  },
});
