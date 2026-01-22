// Test: pred must be PredNode with 'op' field
import { definePlan } from '@ranking-dsl/runtime';

export default definePlan({
  name: 'bad_pred_type',
  build: (ctx) => {
    const source = ctx.viewer({ endpoint: EP.redis.redis_default })
      .follow({ endpoint: EP.redis.redis_default, fanout: 100 });
    const filtered = source.filter({
      pred: { invalid: "structure" } as any,
    });
    return filtered.take({ count: 10 });
  },
});
