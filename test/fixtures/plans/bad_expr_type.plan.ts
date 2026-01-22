// Test: expr must be ExprNode or ExprPlaceholder, not string
import { definePlan } from '@ranking-dsl/runtime';

export default definePlan({
  name: 'bad_expr_type',
  build: (ctx) => {
    const source = ctx.viewer({ endpoint: EP.redis.redis_default })
      .follow({ endpoint: EP.redis.redis_default, fanout: 100 });
    const scored = source.vm({
      outKey: Key.final_score,
      expr: "not an expression" as any,
    });
    return scored.take({ count: 10 });
  },
});
