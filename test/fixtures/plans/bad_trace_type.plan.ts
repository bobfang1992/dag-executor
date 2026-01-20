// Test: trace must be string or null, not number
import { definePlan } from '@ranking-dsl/runtime';

export default definePlan({
  name: 'bad_trace_type',
  build: (ctx) => {
    const source = ctx.viewer.follow({ fanout: 100, trace: 123 as any });
    return source.take({ count: 10 });
  },
});
