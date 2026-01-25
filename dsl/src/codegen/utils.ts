// Utility functions for codegen

import { createHash } from "crypto";
import { execSync } from "child_process";

// =====================================================
// Digest Computation
// =====================================================

export function stableStringify(obj: unknown): string {
  if (obj === null) return "null";
  if (typeof obj === "boolean" || typeof obj === "number") return JSON.stringify(obj);
  if (typeof obj === "string") return JSON.stringify(obj);
  if (Array.isArray(obj)) {
    return "[" + obj.map(stableStringify).join(",") + "]";
  }
  if (typeof obj === "object") {
    const keys = Object.keys(obj as Record<string, unknown>).sort();
    const pairs = keys.map(k => `${JSON.stringify(k)}:${stableStringify((obj as Record<string, unknown>)[k])}`);
    return "{" + pairs.join(",") + "}";
  }
  throw new Error(`Cannot stringify: ${typeof obj}`);
}

export function computeDigest(obj: unknown): string {
  const canonical = stableStringify(obj);
  return createHash("sha256").update(canonical).digest("hex");
}

// =====================================================
// C++ Formatting
// =====================================================

export function formatCpp(code: string): string {
  try {
    return execSync("clang-format", {
      input: code,
      encoding: "utf-8",
      maxBuffer: 10 * 1024 * 1024,
    });
  } catch {
    throw new Error(
      "clang-format is required but not found.\n" +
      "Install it via: brew install clang-format (macOS) or apt install clang-format (Linux)"
    );
  }
}

// =====================================================
// Name Conversion Helpers
// =====================================================

/** Convert C++ snake_case to TypeScript camelCase */
export function cppNameToTsName(cppName: string): string {
  // viewer.follow -> viewerFollow
  // fetch_cached_recommendation -> fetchCachedRecommendation
  return cppName
    .split(".")
    .map((part, i) => {
      const camel = part.replace(/_([a-z])/g, (_, c) => c.toUpperCase());
      return i === 0 ? camel : camel.charAt(0).toUpperCase() + camel.slice(1);
    })
    .join("");
}

/**
 * Convert op name to TypeScript interface name.
 * Handles namespaced ops with "::" separator.
 *
 * Examples:
 *   "core::filter" -> "CoreFilterOpts"
 *   "test::sleep" -> "TestSleepOpts"
 *   "viewer.follow" -> "ViewerFollowOpts" (legacy)
 *   "vm" -> "VmOpts"
 */
export function opToInterfaceName(op: string): string {
  // Split by "::" (namespace) or "." (legacy)
  const parts = op.split(/::|\./).filter(Boolean);
  const pascalParts = parts.map(part => {
    const camel = part.replace(/_([a-z])/g, (_, c) => c.toUpperCase());
    return camel.charAt(0).toUpperCase() + camel.slice(1);
  });
  return pascalParts.join("") + "Opts";
}

/**
 * Convert op name to method name (local name without namespace).
 * Handles namespaced ops with "::" separator.
 *
 * Examples:
 *   "core::filter" -> "filter"
 *   "test::sleep" -> "sleep"
 *   "viewer.follow" -> "follow" (legacy)
 *   "vm" -> "vm"
 */
export function opToMethodName(op: string): string {
  // Split by "::" (namespace) or "." (legacy)
  const parts = op.split(/::|\./).filter(Boolean);
  const lastPart = parts[parts.length - 1];
  return lastPart.replace(/_([a-z])/g, (_, c) => c.toUpperCase());
}

/**
 * Get namespace from qualified op name.
 * Returns undefined for unqualified ops.
 *
 * Examples:
 *   "core::filter" -> "core"
 *   "test::sleep" -> "test"
 *   "vm" -> undefined
 */
export function opToNamespace(op: string): string | undefined {
  const colonIdx = op.indexOf("::");
  if (colonIdx === -1) return undefined;
  return op.substring(0, colonIdx);
}

/**
 * Convert TypeScript camelCase method name back to snake_case.
 * This is the inverse of opToMethodName's snake_case -> camelCase conversion.
 *
 * Examples:
 *   "fixedSource" -> "fixed_source"
 *   "busyCpu" -> "busy_cpu"
 *   "vm" -> "vm"
 */
export function methodNameToSnakeCase(methodName: string): string {
  return methodName.replace(/[A-Z]/g, letter => `_${letter.toLowerCase()}`);
}

/**
 * Get user-friendly property name for task options interface.
 * For expr_id and pred_id params, use shorter friendly names (expr, pred).
 */
export function friendlyParamName(paramName: string, paramType: string): string {
  // Use friendly names for expression/predicate params
  if (paramType === "expr_id") return "expr";
  if (paramType === "pred_id") return "pred";
  // Otherwise use standard camelCase conversion
  return cppNameToTsName(paramName);
}

// =====================================================
// C++ Type/Status Helpers
// =====================================================

import type { Status, CapabilityStatus } from "./types.js";

export function cppType(t: string): string {
  switch (t) {
    case "int": return "int64_t";
    case "float": return "double";
    case "string": return "std::string";
    case "bool": return "bool";
    case "feature_bundle": return "FeatureBundle";
    default: throw new Error(`Unknown type: ${t}`);
  }
}

export function cppStatus(s: Status): string {
  switch (s) {
    case "active": return "Status::Active";
    case "deprecated": return "Status::Deprecated";
    case "blocked": return "Status::Blocked";
    default: throw new Error(`Unknown status: ${s}`);
  }
}

export function cppCapabilityStatus(s: CapabilityStatus): string {
  switch (s) {
    case "implemented": return "CapabilityStatus::Implemented";
    case "draft": return "CapabilityStatus::Draft";
    case "deprecated": return "CapabilityStatus::Deprecated";
    case "blocked": return "CapabilityStatus::Blocked";
    default: throw new Error(`Unknown capability status: ${s}`);
  }
}

export function cppPropertyType(jsonType: string | undefined): string {
  switch (jsonType) {
    case "string": return "SchemaPropertyType::String";
    case "integer":
    case "number": return "SchemaPropertyType::Number";
    case "boolean": return "SchemaPropertyType::Boolean";
    case "object": return "SchemaPropertyType::Object";
    case "array": return "SchemaPropertyType::Array";
    default: return "SchemaPropertyType::Unknown";
  }
}

export function cppCapabilityIdent(id: string): string {
  // cap.rfc.0001.extensions_capabilities.v1 -> kCapRfc0001ExtensionsCapabilitiesV1
  const parts = id.split(".");
  const pascal = parts.map(p => p.charAt(0).toUpperCase() + p.slice(1).replace(/_([a-z])/g, (_, c) => c.toUpperCase())).join("");
  return "k" + pascal;
}
