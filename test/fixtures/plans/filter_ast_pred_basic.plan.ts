/**
 * Test fixture: natural predicate syntax in filter()
 *
 * Uses object form with natural TS predicate:
 *   c.filter({ pred: Key.model_score_1 > 0.5 && Key.country != null })
 *
 * Expected: AST extractor compiles the predicate to PredIR and merges into pred_table.
 */

import { definePlan } from "@ranking-dsl/runtime";
// Key, P are globals injected by the compiler

export default definePlan({
  name: "filter_ast_pred_basic",
  build: (ctx) => {
    return ctx.viewer
      .follow({ fanout: 10, trace: "src" })
      .filter({
        pred: Key.model_score_1 > P.esr_cutoff && Key.country != null,
        trace: "filter_natural",
      })
      .take({ count: 5, trace: "take" });
  },
});
