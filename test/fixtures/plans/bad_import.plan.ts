/**
 * Test fixture: invalid import
 *
 * Plans may only import from @ranking-dsl/runtime, @ranking-dsl/generated,
 * and *.fragment.ts files. This plan tries to import an arbitrary helper.
 */

import { definePlan, Key } from "@ranking-dsl/runtime";
import { someHelper } from "./helpers/scoring";  // Not allowed!

export default definePlan({
  name: "bad_import",
  build: (ctx) => {
    someHelper();  // Use the import
    return ctx.viewer
      .follow({ fanout: 10, trace: "src" })
      .take({ count: 5, trace: "take" });
  },
});
