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
