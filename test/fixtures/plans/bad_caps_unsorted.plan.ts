/**
 * Test fixture: capabilities_required not sorted.
 * Should fail validation with "must be sorted and unique".
 */

// Directly emit a bad artifact to test validation
const badArtifact = {
  schema_version: 1,
  plan_name: "bad_caps_unsorted",
  nodes: [
    {
      node_id: "n0",
      op: "viewer.follow",
      inputs: [],
      params: { fanout: 10, trace: null },
    },
  ],
  outputs: ["n0"],
  // NOT sorted - "b" comes before "a"
  capabilities_required: ["cap.b", "cap.a"],
};

// QuickJS mode: emit directly
if (typeof globalThis !== "undefined" && "__emitPlan" in globalThis) {
  const emitFn = (globalThis as Record<string, unknown>).__emitPlan;
  if (typeof emitFn === "function") {
    emitFn(badArtifact);
  }
}

export default {};
