/**
 * Test fixture: valid capabilities and extensions.
 * Should compile successfully - demonstrates RFC0001 functionality.
 */
import { definePlan } from "@ranking-dsl/runtime";
import { Key } from "@ranking-dsl/generated";

export default definePlan({
  name: "valid_capabilities",
  build: (ctx) => {
    // Declare capabilities with payload
    ctx.requireCapability("cap.debug", { level: "verbose" });
    ctx.requireCapability("cap.audit");

    // Create a node with extensions
    const candidates = ctx.viewer.follow({
      fanout: 10,
      trace: "src",
      extensions: {
        "cap.debug": { node_debug: true },
      },
    });

    // Return with no extensions
    return candidates.take({
      count: 5,
      extensions: {
        "cap.audit": { audit_take: true },
      },
    });
  },
});
