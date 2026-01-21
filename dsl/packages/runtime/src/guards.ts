/**
 * No-undefined runtime guards.
 * All DSL APIs must reject undefined to ensure clean JSON serialization.
 */

export function assertNotUndefined<T>(value: T, path: string): asserts value is Exclude<T, undefined> {
  if (value === undefined) {
    throw new Error(`Undefined is not allowed in DSL inputs: ${path}`);
  }
}

export function assertObject(value: unknown, path: string): asserts value is Record<string, unknown> {
  if (typeof value !== "object" || value === null || Array.isArray(value)) {
    throw new Error(`Expected object at ${path}, got ${typeof value}`);
  }
}

export function checkNoUndefined(obj: Record<string, unknown>, path: string): void {
  for (const [key, value] of Object.entries(obj)) {
    assertNotUndefined(value, `${path}.${key}`);
    if (typeof value === "object" && value !== null && !Array.isArray(value)) {
      checkNoUndefined(value as Record<string, unknown>, `${path}.${key}`);
    }
  }
}

export function assertInteger(value: unknown, path: string): asserts value is number {
  if (typeof value !== "number" || !Number.isInteger(value)) {
    throw new Error(`${path} must be an integer, got ${typeof value === "number" ? value : typeof value}`);
  }
}

export function assertStringOrNull(value: unknown, path: string): void {
  if (value !== null && typeof value !== "string") {
    throw new Error(`${path} must be a string or null, got ${typeof value}`);
  }
}

/**
 * Assert that value is a valid EndpointId.
 * Must start with "ep_" and be at most 64 characters (matches registry validation).
 */
export function assertEndpointId(value: unknown, path: string): void {
  if (typeof value !== "string") {
    throw new Error(`${path} must be a string (EndpointId), got ${typeof value}`);
  }
  if (!value.startsWith("ep_")) {
    throw new Error(`${path} must start with "ep_", got "${value}"`);
  }
  if (value.length > 64) {
    throw new Error(`${path} too long (max 64 chars), got ${value.length}`);
  }
}

// ============================================================
// RFC0001: Capabilities and Extensions validation helpers
// ============================================================

/**
 * Normalize capabilities_required: sort lexicographically and dedupe.
 */
export function normalizeCapabilitiesRequired(ids: string[]): string[] {
  return [...new Set(ids)].sort();
}

/**
 * Check if a string array is sorted and unique.
 */
export function isSortedUnique(arr: string[]): boolean {
  for (let i = 1; i < arr.length; i++) {
    if (arr[i] <= arr[i - 1]) {
      return false;
    }
  }
  return true;
}

/**
 * Validate capabilities_required and extensions at plan level.
 * - capabilities_required must be sorted + unique
 * - extensions must be a plain object (not array/null)
 * - every extensions key must appear in capabilities_required
 */
export function validateCapabilitiesAndExtensions(
  capabilitiesRequired: unknown,
  extensions: unknown,
  where: string
): void {
  // Validate capabilities_required if present
  if (capabilitiesRequired !== undefined) {
    if (!Array.isArray(capabilitiesRequired)) {
      throw new Error(
        `${where}: capabilities_required must be an array`
      );
    }
    for (let i = 0; i < capabilitiesRequired.length; i++) {
      if (typeof capabilitiesRequired[i] !== "string") {
        throw new Error(
          `${where}: capabilities_required[${i}] must be a string`
        );
      }
    }
    if (!isSortedUnique(capabilitiesRequired as string[])) {
      throw new Error(
        `${where}: capabilities_required must be sorted and unique`
      );
    }
  }

  // Validate extensions if present
  if (extensions !== undefined) {
    if (extensions === null || typeof extensions !== "object" || Array.isArray(extensions)) {
      throw new Error(
        `${where}: extensions must be a plain object (not null or array)`
      );
    }

    const caps = new Set(capabilitiesRequired as string[] | undefined ?? []);
    for (const key of Object.keys(extensions as Record<string, unknown>)) {
      if (!caps.has(key)) {
        throw new Error(
          `${where}: extensions key "${key}" must appear in capabilities_required`
        );
      }
    }
  }
}

/**
 * Validate node-level extensions against plan capabilities_required.
 * Every key in node.extensions must appear in plan capabilities_required.
 */
export function validateNodeExtensions(
  nodeExtensions: unknown,
  planCapabilities: string[] | undefined,
  nodeId: string
): void {
  if (nodeExtensions === undefined) {
    return;
  }

  if (nodeExtensions === null || typeof nodeExtensions !== "object" || Array.isArray(nodeExtensions)) {
    throw new Error(
      `node[${nodeId}]: extensions must be a plain object (not null or array)`
    );
  }

  const caps = new Set(planCapabilities ?? []);
  for (const key of Object.keys(nodeExtensions as Record<string, unknown>)) {
    if (!caps.has(key)) {
      throw new Error(
        `node[${nodeId}]: extension key "${key}" requires plan capability "${key}"`
      );
    }
  }
}
