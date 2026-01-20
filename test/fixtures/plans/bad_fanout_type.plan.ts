// Test: fanout must be integer, not string
import { definePlan } from '@ranking-dsl/runtime';

export default definePlan({
  name: 'bad_fanout_type',
  build: (ctx) => {
    const source = ctx.viewer.follow({ fanout: "100" as any });
    return source.take({ count: 10 });
  },
});
