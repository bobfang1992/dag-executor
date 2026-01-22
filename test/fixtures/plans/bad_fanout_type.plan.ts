// Test: fanout must be integer, not string
import { definePlan } from '@ranking-dsl/runtime';

export default definePlan({
  name: 'bad_fanout_type',
  build: (ctx) => {
    const source = ctx.viewer({ endpoint: EP.redis.redis_default })
      .follow({ endpoint: EP.redis.redis_default, fanout: "100" as any });
    return source.take({ count: 10 });
  },
});
