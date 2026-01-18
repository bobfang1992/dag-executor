/**
 * Deterministic JSON stringification with stable key ordering.
 */

export function stableStringify(value: unknown, indent = 2): string {
  return JSON.stringify(value, stableReplacer, indent);
}

function stableReplacer(_key: string, value: unknown): unknown {
  if (value === null || typeof value !== "object") {
    return value;
  }

  if (Array.isArray(value)) {
    return value;
  }

  // Sort object keys for stable output
  const sorted: Record<string, unknown> = {};
  const keys = Object.keys(value as Record<string, unknown>).sort();
  for (const key of keys) {
    sorted[key] = (value as Record<string, unknown>)[key];
  }
  return sorted;
}
