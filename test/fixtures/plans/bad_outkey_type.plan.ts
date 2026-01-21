// Test: outKey must be KeyToken, not a number/NaN
import { definePlan, E } from '@ranking-dsl/runtime';

export default definePlan({
  name: 'bad_outkey_type',
  build: (ctx) => {
    const source = ctx.viewer({ endpoint: EP.redis.default })
      .follow({ endpoint: EP.redis.default, fanout: 100 });
    // Key.final_score / 2 evaluates to NaN, not a KeyToken
    const scored = source.vm({
      outKey: Key.final_score / 2 as any,
      expr: E.const(1),
    });
    return scored.take({ count: 10 });
  },
});
