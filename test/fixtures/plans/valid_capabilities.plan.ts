/**
 * Test fixture: valid capabilities and extensions.
 * Should compile successfully - demonstrates RFC0001 functionality.
 */
import { definePlan } from "@ranking-dsl/runtime";
// Key, P, coalesce are globals injected by the compiler

export default definePlan({
  name: "valid_capabilities",
  build: (ctx) => {
    // Declare the base extensions/capabilities capability (RFC0001)
    ctx.requireCapability("cap.rfc.0001.extensions_capabilities.v1");

    // Create a node with extensions (empty payload as per schema)
    const candidates = ctx.viewer({ endpoint: EP.redis.default }).follow({
      endpoint: EP.redis.default,
      fanout: 10,
      trace: "src",
      extensions: {
        "cap.rfc.0001.extensions_capabilities.v1": {},
      },
    });

    // Return with extensions
    return candidates.take({
      count: 5,
      extensions: {
        "cap.rfc.0001.extensions_capabilities.v1": {},
      },
    });
  },
});
