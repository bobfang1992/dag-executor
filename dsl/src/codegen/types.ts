// AUTO-GENERATED section markers removed - this is a source file
// Type definitions for codegen

// =====================================================
// Type Definitions
// =====================================================

export type KeyType = "int" | "float" | "string" | "bool" | "feature_bundle";
export type ParamType = "int" | "float" | "string" | "bool";
export type FeatureType = "int" | "float" | "string" | "bool";
export type Status = "active" | "deprecated" | "blocked";
export type CapabilityStatus = "implemented" | "draft" | "deprecated" | "blocked";
export type TaskParamType = "int" | "float" | "bool" | "string" | "expr_id" | "pred_id" | "node_ref";

export interface KeyEntry {
  key_id: number;
  name: string;
  type: KeyType;
  nullable: boolean;
  doc: string;
  status: Status;
  allow_read: boolean;
  allow_write: boolean;
  replaced_by: number | null;
  default: number | string | boolean | null;
}

export interface ParamEntry {
  param_id: number;
  name: string;
  type: ParamType;
  nullable: boolean;
  doc: string;
  status: Status;
  allow_read: boolean;
  allow_write: boolean;
  replaced_by: number | null;
}

export interface FeatureEntry {
  feature_id: number;
  stage: string;
  name: string;
  type: FeatureType;
  nullable: boolean;
  status: Status;
  doc: string;
}

// JSON Schema subset for capability payload validation
export interface JsonSchema {
  type?: string;
  properties?: Record<string, JsonSchema>;
  additionalProperties?: boolean;
  required?: string[];
}

export interface CapabilityEntry {
  id: string;           // "cap.rfc.0001.extensions_capabilities.v1"
  rfc: string;          // "0001"
  name: string;         // "extensions_capabilities"
  status: CapabilityStatus;
  doc: string;
  payload_schema: JsonSchema | null;  // Parsed JSON Schema or null (no payload allowed)
}

export interface ValidationRules {
  schema_version: number;
  patterns: Record<string, string>;
  limits: Record<string, number>;
  enums: Record<string, string[]>;
}

export interface TaskParamEntry {
  name: string;
  type: TaskParamType;
  required: boolean;
  nullable: boolean;
}

export interface TaskEntry {
  op: string;
  output_pattern: string;
  params: TaskParamEntry[];
  writes_effect?: unknown; // JSON parsed from triple-quoted string
}

export interface TaskRegistry {
  schema_version: number;
  manifest_digest: string;
  tasks: TaskEntry[];
}

// =====================================================
// Type Constants
// =====================================================

export const KEY_TYPES: readonly string[] = ["int", "float", "string", "bool", "feature_bundle"];
export const PARAM_TYPES: readonly string[] = ["int", "float", "string", "bool"];
export const FEATURE_TYPES: readonly string[] = ["int", "float", "string", "bool"];
export const STATUSES: readonly string[] = ["active", "deprecated", "blocked"];
export const CAPABILITY_STATUSES: readonly string[] = ["implemented", "draft", "deprecated", "blocked"];
export const TASK_PARAM_TYPES: readonly string[] = ["int", "float", "bool", "string", "expr_id", "pred_id", "node_ref"];

// =====================================================
// Type Guards
// =====================================================

export function isKeyType(v: unknown): v is KeyType {
  return typeof v === "string" && KEY_TYPES.includes(v);
}

export function isParamType(v: unknown): v is ParamType {
  return typeof v === "string" && PARAM_TYPES.includes(v);
}

export function isFeatureType(v: unknown): v is FeatureType {
  return typeof v === "string" && FEATURE_TYPES.includes(v);
}

export function isStatus(v: unknown): v is Status {
  return typeof v === "string" && STATUSES.includes(v);
}

export function isCapabilityStatus(v: unknown): v is CapabilityStatus {
  return typeof v === "string" && CAPABILITY_STATUSES.includes(v);
}

export function isTaskParamType(v: unknown): v is TaskParamType {
  return typeof v === "string" && TASK_PARAM_TYPES.includes(v);
}

// =====================================================
// Assertion Helpers
// =====================================================

export function assertString(v: unknown, field: string): string {
  if (typeof v !== "string") throw new Error(`${field} must be a string`);
  return v;
}

export function assertNumber(v: unknown, field: string): number {
  if (typeof v !== "number") throw new Error(`${field} must be a number`);
  return v;
}

export function assertBoolean(v: unknown, field: string): boolean {
  if (typeof v !== "boolean") throw new Error(`${field} must be a boolean`);
  return v;
}
