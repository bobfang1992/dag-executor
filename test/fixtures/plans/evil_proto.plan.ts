/**
 * Evil plan - tests Function constructor bypass via prototype.
 *
 * This plan attempts to access Function via (() => {}).constructor
 * which should be disabled in the sandbox.
 * Expected: dslc should fail with non-zero exit code.
 */

import { definePlan } from "@ranking-dsl/runtime";

export default definePlan({
  name: "evil_proto_plan",
  build: (ctx) => {
    // Try to bypass Function block via prototype
    // This should fail in QuickJS sandbox
    // @ts-expect-error - intentionally accessing constructor
    const Fn = (() => {}).constructor;
    // @ts-expect-error - intentionally using Function constructor
    const evil = new Fn("return 1 + 1");
    console.log(evil());

    // This line should never be reached
    return ctx.viewer({ endpoint: EP.redis.redis_default }).follow({ endpoint: EP.redis.redis_default, fanout: 1 });
  },
});
