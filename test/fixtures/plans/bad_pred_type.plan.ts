// Test: pred must be PredNode with 'op' field
import { definePlan, EP } from '@ranking-dsl/runtime';

export default definePlan({
  name: 'bad_pred_type',
  build: (ctx) => {
    const source = ctx.follow({ endpoint: EP.redis.default, fanout: 100 });
    const filtered = source.filter({
      pred: { invalid: "structure" } as any,
    });
    return filtered.take({ count: 10 });
  },
});
