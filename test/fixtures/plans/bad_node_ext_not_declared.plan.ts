/**
 * Test fixture: node has extensions but plan has no capabilities_required.
 * Should fail validation with "requires plan capability".
 */

// Directly emit a bad artifact to test validation
const badArtifact = {
  schema_version: 1,
  plan_name: "bad_node_ext_not_declared",
  nodes: [
    {
      node_id: "n0",
      op: "viewer.follow",
      inputs: [],
      params: { fanout: 10, trace: null },
      // Node has extensions but plan does not declare this capability
      extensions: {
        "cap.rfc.X": { debug: true },
      },
    },
  ],
  outputs: ["n0"],
  // No capabilities_required - node extension should fail
};

// QuickJS mode: emit directly
if (typeof globalThis !== "undefined" && "__emitPlan" in globalThis) {
  const emitFn = (globalThis as Record<string, unknown>).__emitPlan;
  if (typeof emitFn === "function") {
    emitFn(badArtifact);
  }
}

export default {};
