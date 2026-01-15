/**
 * Test fixture: extensions key not in capabilities_required.
 * Should fail validation with "must appear in capabilities_required".
 */

// Directly emit a bad artifact to test validation
const badArtifact = {
  schema_version: 1,
  plan_name: "bad_ext_key_not_required",
  nodes: [
    {
      node_id: "n0",
      op: "viewer.follow",
      inputs: [],
      params: { fanout: 10, trace: null },
    },
  ],
  outputs: ["n0"],
  // extensions key "cap.rfc.X" is not in capabilities_required
  capabilities_required: ["cap.a"],
  extensions: {
    "cap.rfc.X": { some: "config" },
  },
};

// QuickJS mode: emit directly
if (typeof globalThis !== "undefined" && "__emitPlan" in globalThis) {
  const emitFn = (globalThis as Record<string, unknown>).__emitPlan;
  if (typeof emitFn === "function") {
    emitFn(badArtifact);
  }
}

export default {};
