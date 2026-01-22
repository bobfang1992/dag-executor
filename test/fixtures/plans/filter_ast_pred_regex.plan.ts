/**
 * Test fixture: natural predicate syntax with regex() in filter()
 *
 * Uses object form with regex() function:
 *   c.filter({ pred: regex(Key.title, "^test") })
 *
 * Expected: AST extractor compiles the predicate to PredIR with regex op.
 */

import { definePlan } from "@ranking-dsl/runtime";
// Key, regex are globals injected by the compiler

export default definePlan({
  name: "filter_ast_pred_regex",
  build: (ctx) => {
    return ctx.viewer({ endpoint: EP.redis.redis_default })
      .follow({ endpoint: EP.redis.redis_default, fanout: 10, trace: "src" })
      .filter({
        pred: regex(Key.title, "^test.*pattern"),
        trace: "filter_regex",
      })
      .take({ count: 5, trace: "take" });
  },
});
