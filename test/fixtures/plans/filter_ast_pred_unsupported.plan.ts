/**
 * Test fixture: unsupported predicate syntax should fail compilation.
 *
 * This uses arithmetic in predicates which is not supported:
 *   c.filter({ pred: (Key.model_score_1 * 2) > 10 })
 *
 * Expected: AST extractor rejects this and produces a compilation error.
 * Arithmetic expressions are not allowed inside predicate comparisons.
 */

import { definePlan } from "@ranking-dsl/runtime";
// Key is a global injected by the compiler

export default definePlan({
  name: "filter_ast_pred_unsupported",
  build: (ctx) => {
    return ctx.viewer({ endpoint: EP.redis.default })
      .follow({ endpoint: EP.redis.default, fanout: 10, trace: "src" })
      .filter({
        // This is unsupported: arithmetic in predicates
        pred: (Key.model_score_1 * 2) > 10,
        trace: "filter_bad",
      })
      .take({ count: 5, trace: "take" });
  },
});
