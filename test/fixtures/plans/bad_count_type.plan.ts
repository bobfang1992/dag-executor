// Test: count must be integer, not null
import { definePlan, EP } from '@ranking-dsl/runtime';

export default definePlan({
  name: 'bad_count_type',
  build: (ctx) => {
    const source = ctx.follow({ endpoint: EP.redis.default, fanout: 100 });
    return source.take({ count: null as any });
  },
});
