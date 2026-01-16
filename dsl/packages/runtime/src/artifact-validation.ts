/**
 * Shared artifact validation for both QuickJS and Node compilers.
 *
 * IMPORTANT: Any validation changes must be made here to ensure parity
 * between compilers. Do NOT add validation directly to compiler CLIs.
 */

import { validatePayload, SUPPORTED_CAPABILITIES, CAPABILITY_REGISTRY } from "@ranking-dsl/generated";
import {
  validateCapabilitiesAndExtensions,
  validateNodeExtensions,
} from "./guards.js";

/**
 * Validate that artifact conforms to PlanArtifact schema.
 * This is the single source of truth for artifact validation.
 * Both QuickJS and Node compilers must use this function.
 */
export function validateArtifact(artifact: unknown, planFileName: string): void {
  if (artifact === null || typeof artifact !== "object") {
    throw new Error(`Invalid artifact from ${planFileName}: not an object`);
  }

  const obj = artifact as Record<string, unknown>;

  // ============================================================
  // Required fields
  // ============================================================

  if (typeof obj.schema_version !== "number") {
    throw new Error(
      `Invalid artifact from ${planFileName}: missing or invalid schema_version (expected number)`
    );
  }
  if (typeof obj.plan_name !== "string" || obj.plan_name.length === 0) {
    throw new Error(
      `Invalid artifact from ${planFileName}: missing or invalid plan_name (expected non-empty string)`
    );
  }
  if (!Array.isArray(obj.nodes)) {
    throw new Error(
      `Invalid artifact from ${planFileName}: missing or invalid nodes (expected array)`
    );
  }
  if (!Array.isArray(obj.outputs)) {
    throw new Error(
      `Invalid artifact from ${planFileName}: missing or invalid outputs (expected array)`
    );
  }

  // ============================================================
  // Node validation (first pass: collect IDs and validate structure)
  // ============================================================

  const nodeIds = new Set<string>();
  for (let i = 0; i < obj.nodes.length; i++) {
    const node = obj.nodes[i];
    if (node === null || typeof node !== "object") {
      throw new Error(
        `Invalid artifact from ${planFileName}: nodes[${i}] is not an object`
      );
    }
    const n = node as Record<string, unknown>;
    if (typeof n.node_id !== "string") {
      throw new Error(
        `Invalid artifact from ${planFileName}: nodes[${i}].node_id missing or invalid`
      );
    }
    if (typeof n.op !== "string") {
      throw new Error(
        `Invalid artifact from ${planFileName}: nodes[${i}].op missing or invalid`
      );
    }
    if (!Array.isArray(n.inputs)) {
      throw new Error(
        `Invalid artifact from ${planFileName}: nodes[${i}].inputs missing or invalid`
      );
    }
    nodeIds.add(n.node_id);
  }

  // ============================================================
  // Node validation (second pass: validate input references)
  // ============================================================

  for (let i = 0; i < obj.nodes.length; i++) {
    const n = obj.nodes[i] as Record<string, unknown>;
    const inputs = n.inputs as unknown[];
    for (let j = 0; j < inputs.length; j++) {
      if (typeof inputs[j] !== "string") {
        throw new Error(
          `Invalid artifact from ${planFileName}: nodes[${i}].inputs[${j}] is not a string`
        );
      }
      if (!nodeIds.has(inputs[j] as string)) {
        throw new Error(
          `Invalid artifact from ${planFileName}: nodes[${i}].inputs[${j}] references unknown node "${inputs[j]}"`
        );
      }
    }
  }

  // ============================================================
  // Output validation
  // ============================================================

  for (let i = 0; i < obj.outputs.length; i++) {
    if (typeof obj.outputs[i] !== "string") {
      throw new Error(
        `Invalid artifact from ${planFileName}: outputs[${i}] is not a string`
      );
    }
    if (!nodeIds.has(obj.outputs[i] as string)) {
      throw new Error(
        `Invalid artifact from ${planFileName}: outputs[${i}] references unknown node "${obj.outputs[i]}"`
      );
    }
  }

  // ============================================================
  // Optional fields: expr_table, pred_table
  // ============================================================

  if (obj.expr_table !== undefined && (obj.expr_table === null || typeof obj.expr_table !== "object")) {
    throw new Error(
      `Invalid artifact from ${planFileName}: expr_table must be an object if present`
    );
  }
  if (obj.pred_table !== undefined && (obj.pred_table === null || typeof obj.pred_table !== "object")) {
    throw new Error(
      `Invalid artifact from ${planFileName}: pred_table must be an object if present`
    );
  }

  // ============================================================
  // RFC0001: Capabilities and extensions validation
  // ============================================================

  validateCapabilitiesAndExtensions(
    obj.capabilities_required,
    obj.extensions,
    `artifact from ${planFileName}`
  );

  // Validate extension payloads against their schemas
  const extensions = (obj.extensions ?? {}) as Record<string, unknown>;
  for (const [capId, payload] of Object.entries(extensions)) {
    validatePayload(capId, payload);
  }

  // Ensure capabilities with required fields have extensions entries
  const capsRequired = (obj.capabilities_required ?? []) as string[];
  for (const capId of capsRequired) {
    const meta = CAPABILITY_REGISTRY[capId];
    if (meta?.payloadSchema?.required && meta.payloadSchema.required.length > 0) {
      if (!(capId in extensions)) {
        throw new Error(
          `capability '${capId}' has required fields but no extensions entry`
        );
      }
    }
  }

  // Validate node-level extensions
  const planCapabilities = obj.capabilities_required as string[] | undefined;
  for (let i = 0; i < obj.nodes.length; i++) {
    const n = obj.nodes[i] as Record<string, unknown>;
    validateNodeExtensions(n.extensions, planCapabilities, n.node_id as string);

    // Validate node extension payloads against their schemas
    if (n.extensions && typeof n.extensions === "object") {
      for (const [capId, payload] of Object.entries(n.extensions as Record<string, unknown>)) {
        validatePayload(capId, payload);
      }
    }
  }
}

// Re-export for convenience
export { SUPPORTED_CAPABILITIES };
