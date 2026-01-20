import { readFileSync, writeFileSync, existsSync } from "fs";
import { createHash } from "crypto";
import { execSync } from "child_process";
import { parse as parseToml } from "@iarna/toml";
import path from "path";

// ============================================================
// Type Definitions
// ============================================================

type KeyType = "int" | "float" | "string" | "bool" | "feature_bundle";
type ParamType = "int" | "float" | "string" | "bool";
type FeatureType = "int" | "float" | "string" | "bool";
type Status = "active" | "deprecated" | "blocked";
type CapabilityStatus = "implemented" | "draft" | "deprecated" | "blocked";

interface KeyEntry {
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

interface ParamEntry {
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

interface FeatureEntry {
  feature_id: number;
  stage: string;
  name: string;
  type: FeatureType;
  nullable: boolean;
  status: Status;
  doc: string;
}

// JSON Schema subset for capability payload validation
interface JsonSchema {
  type?: string;
  properties?: Record<string, JsonSchema>;
  additionalProperties?: boolean;
  required?: string[];
}

interface CapabilityEntry {
  id: string;           // "cap.rfc.0001.extensions_capabilities.v1"
  rfc: string;          // "0001"
  name: string;         // "extensions_capabilities"
  status: CapabilityStatus;
  doc: string;
  payload_schema: JsonSchema | null;  // Parsed JSON Schema or null (no payload allowed)
}

interface ValidationRules {
  schema_version: number;
  patterns: Record<string, string>;
  limits: Record<string, number>;
  enums: Record<string, string[]>;
}

type TaskParamType = "int" | "float" | "bool" | "string" | "expr_id" | "pred_id";

interface TaskParamEntry {
  name: string;
  type: TaskParamType;
  required: boolean;
  nullable: boolean;
}

interface TaskEntry {
  op: string;
  output_pattern: string;
  params: TaskParamEntry[];
  writes_effect?: unknown; // JSON parsed from triple-quoted string
}

interface TaskRegistry {
  schema_version: number;
  manifest_digest: string;
  tasks: TaskEntry[];
}

// ============================================================
// Validation Helpers
// ============================================================

const KEY_TYPES: readonly string[] = ["int", "float", "string", "bool", "feature_bundle"];
const PARAM_TYPES: readonly string[] = ["int", "float", "string", "bool"];
const FEATURE_TYPES: readonly string[] = ["int", "float", "string", "bool"];
const STATUSES: readonly string[] = ["active", "deprecated", "blocked"];
const CAPABILITY_STATUSES: readonly string[] = ["implemented", "draft", "deprecated", "blocked"];
const TASK_PARAM_TYPES: readonly string[] = ["int", "float", "bool", "string", "expr_id", "pred_id"];

function isKeyType(v: unknown): v is KeyType {
  return typeof v === "string" && KEY_TYPES.includes(v);
}

function isParamType(v: unknown): v is ParamType {
  return typeof v === "string" && PARAM_TYPES.includes(v);
}

function isFeatureType(v: unknown): v is FeatureType {
  return typeof v === "string" && FEATURE_TYPES.includes(v);
}

function isStatus(v: unknown): v is Status {
  return typeof v === "string" && STATUSES.includes(v);
}

function isCapabilityStatus(v: unknown): v is CapabilityStatus {
  return typeof v === "string" && CAPABILITY_STATUSES.includes(v);
}

function isTaskParamType(v: unknown): v is TaskParamType {
  return typeof v === "string" && TASK_PARAM_TYPES.includes(v);
}

function assertString(v: unknown, field: string): string {
  if (typeof v !== "string") throw new Error(`${field} must be a string`);
  return v;
}

function assertNumber(v: unknown, field: string): number {
  if (typeof v !== "number") throw new Error(`${field} must be a number`);
  return v;
}

function assertBoolean(v: unknown, field: string): boolean {
  if (typeof v !== "boolean") throw new Error(`${field} must be a boolean`);
  return v;
}

// ============================================================
// Parsing Functions
// ============================================================

function parseKeys(tomlPath: string): KeyEntry[] {
  const content = readFileSync(tomlPath, "utf-8");
  const parsed: unknown = parseToml(content);

  if (typeof parsed !== "object" || parsed === null) {
    throw new Error("Invalid TOML structure");
  }

  const obj = parsed as Record<string, unknown>;
  const rawKeys = obj["key"];
  if (!Array.isArray(rawKeys)) {
    throw new Error("Expected [[key]] array in keys.toml");
  }

  const entries: KeyEntry[] = [];
  const seenIds = new Set<number>();
  const seenNames = new Set<string>();

  for (const raw of rawKeys) {
    if (typeof raw !== "object" || raw === null) {
      throw new Error("Invalid key entry");
    }
    const r = raw as Record<string, unknown>;

    const key_id = assertNumber(r["key_id"], "key_id");
    const name = assertString(r["name"], "name");
    const typeVal = r["type"];
    if (!isKeyType(typeVal)) throw new Error(`Invalid key type: ${typeVal}`);
    const nullable = assertBoolean(r["nullable"], "nullable");
    const doc = assertString(r["doc"], "doc");

    // Lifecycle fields with defaults
    const status: Status = r["status"] !== undefined
      ? (isStatus(r["status"]) ? r["status"] : (() => { throw new Error(`Invalid status: ${r["status"]}`); })())
      : "active";
    const allow_read = r["allow_read"] !== undefined ? assertBoolean(r["allow_read"], "allow_read") : true;
    const defaultAllowWrite = status === "active";
    const allow_write = r["allow_write"] !== undefined ? assertBoolean(r["allow_write"], "allow_write") : defaultAllowWrite;
    const replaced_by = r["replaced_by"] !== undefined ? assertNumber(r["replaced_by"], "replaced_by") : null;

    // Default value
    let defaultVal: number | string | boolean | null = null;
    if (r["default"] !== undefined) {
      const d = r["default"];
      if (typeof d === "number" || typeof d === "string" || typeof d === "boolean") {
        defaultVal = d;
      } else {
        throw new Error(`Invalid default value for key ${name}`);
      }
    }

    // Validation
    if (seenIds.has(key_id)) throw new Error(`Duplicate key_id: ${key_id}`);
    if (seenNames.has(name)) throw new Error(`Duplicate key name: ${name}`);
    seenIds.add(key_id);
    seenNames.add(name);

    // Key.id special rules
    if (name === "id") {
      if (key_id !== 1) throw new Error("Key.id must have key_id=1");
      if (typeVal !== "int") throw new Error("Key.id must be type int");
      if (nullable !== false) throw new Error("Key.id must be nullable=false");
      if (allow_write !== false) throw new Error("Key.id must have allow_write=false");
    } else {
      // Non-id keys with nullable=false must have default
      if (!nullable && defaultVal === null) {
        throw new Error(`Key ${name} is non-nullable and must have a default value`);
      }
    }

    entries.push({
      key_id, name, type: typeVal, nullable, doc, status, allow_read, allow_write, replaced_by, default: defaultVal
    });
  }

  // Verify Key.id exists
  if (!entries.some(k => k.name === "id")) {
    throw new Error("Key.id must exist in keys.toml");
  }

  // Sort by id
  entries.sort((a, b) => a.key_id - b.key_id);
  return entries;
}

function parseParams(tomlPath: string): ParamEntry[] {
  const content = readFileSync(tomlPath, "utf-8");
  const parsed: unknown = parseToml(content);

  if (typeof parsed !== "object" || parsed === null) {
    throw new Error("Invalid TOML structure");
  }

  const obj = parsed as Record<string, unknown>;
  const rawParams = obj["param"];
  if (!Array.isArray(rawParams)) {
    throw new Error("Expected [[param]] array in params.toml");
  }

  const entries: ParamEntry[] = [];
  const seenIds = new Set<number>();
  const seenNames = new Set<string>();

  for (const raw of rawParams) {
    if (typeof raw !== "object" || raw === null) {
      throw new Error("Invalid param entry");
    }
    const r = raw as Record<string, unknown>;

    const param_id = assertNumber(r["param_id"], "param_id");
    const name = assertString(r["name"], "name");
    const typeVal = r["type"];
    if (!isParamType(typeVal)) throw new Error(`Invalid param type: ${typeVal}`);
    const nullable = assertBoolean(r["nullable"], "nullable");
    const doc = assertString(r["doc"], "doc");

    const status: Status = r["status"] !== undefined
      ? (isStatus(r["status"]) ? r["status"] : (() => { throw new Error(`Invalid status`); })())
      : "active";
    const allow_read = r["allow_read"] !== undefined ? assertBoolean(r["allow_read"], "allow_read") : true;
    const defaultAllowWrite = status === "active";
    const allow_write = r["allow_write"] !== undefined ? assertBoolean(r["allow_write"], "allow_write") : defaultAllowWrite;
    const replaced_by = r["replaced_by"] !== undefined ? assertNumber(r["replaced_by"], "replaced_by") : null;

    if (seenIds.has(param_id)) throw new Error(`Duplicate param_id: ${param_id}`);
    if (seenNames.has(name)) throw new Error(`Duplicate param name: ${name}`);
    seenIds.add(param_id);
    seenNames.add(name);

    entries.push({ param_id, name, type: typeVal, nullable, doc, status, allow_read, allow_write, replaced_by });
  }

  entries.sort((a, b) => a.param_id - b.param_id);
  return entries;
}

function parseFeatures(tomlPath: string): FeatureEntry[] {
  const content = readFileSync(tomlPath, "utf-8");
  const parsed: unknown = parseToml(content);

  if (typeof parsed !== "object" || parsed === null) {
    throw new Error("Invalid TOML structure");
  }

  const obj = parsed as Record<string, unknown>;
  const rawFeatures = obj["feature"];
  if (!Array.isArray(rawFeatures)) {
    throw new Error("Expected [[feature]] array in features.toml");
  }

  const entries: FeatureEntry[] = [];
  const seenIds = new Set<number>();

  for (const raw of rawFeatures) {
    if (typeof raw !== "object" || raw === null) {
      throw new Error("Invalid feature entry");
    }
    const r = raw as Record<string, unknown>;

    const feature_id = assertNumber(r["feature_id"], "feature_id");
    const stage = assertString(r["stage"], "stage");
    const name = assertString(r["name"], "name");
    const typeVal = r["type"];
    if (!isFeatureType(typeVal)) throw new Error(`Invalid feature type: ${typeVal}`);
    const nullable = assertBoolean(r["nullable"], "nullable");
    const status: Status = r["status"] !== undefined
      ? (isStatus(r["status"]) ? r["status"] : (() => { throw new Error(`Invalid status`); })())
      : "active";
    const doc = assertString(r["doc"], "doc");

    if (seenIds.has(feature_id)) throw new Error(`Duplicate feature_id: ${feature_id}`);
    seenIds.add(feature_id);

    entries.push({ feature_id, stage, name, type: typeVal, nullable, status, doc });
  }

  entries.sort((a, b) => a.feature_id - b.feature_id);
  return entries;
}

function parseValidation(tomlPath: string): ValidationRules {
  const content = readFileSync(tomlPath, "utf-8");
  const parsed: unknown = parseToml(content);

  if (typeof parsed !== "object" || parsed === null) {
    throw new Error("Invalid TOML structure in validation.toml");
  }

  const obj = parsed as Record<string, unknown>;

  const schema_version = assertNumber(obj["schema_version"], "schema_version");
  if (schema_version !== 1) {
    throw new Error(`Unsupported validation.toml schema_version: ${schema_version}`);
  }

  // Parse patterns
  const patterns: Record<string, string> = {};
  const rawPatterns = obj["patterns"];
  if (rawPatterns && typeof rawPatterns === "object") {
    for (const [key, val] of Object.entries(rawPatterns as Record<string, unknown>)) {
      patterns[key] = assertString(val, `patterns.${key}`);
    }
  }

  // Parse limits
  const limits: Record<string, number> = {};
  const rawLimits = obj["limits"];
  if (rawLimits && typeof rawLimits === "object") {
    for (const [key, val] of Object.entries(rawLimits as Record<string, unknown>)) {
      limits[key] = assertNumber(val, `limits.${key}`);
    }
  }

  // Parse enums
  const enums: Record<string, string[]> = {};
  const rawEnums = obj["enums"];
  if (rawEnums && typeof rawEnums === "object") {
    for (const [key, val] of Object.entries(rawEnums as Record<string, unknown>)) {
      if (!Array.isArray(val)) {
        throw new Error(`enums.${key} must be an array`);
      }
      enums[key] = val.map((v, i) => assertString(v, `enums.${key}[${i}]`));
    }
  }

  return { schema_version, patterns, limits, enums };
}

function parseTasks(tomlPath: string): TaskRegistry {
  const content = readFileSync(tomlPath, "utf-8");
  const parsed: unknown = parseToml(content);

  if (typeof parsed !== "object" || parsed === null) {
    throw new Error("Invalid TOML structure in tasks.toml");
  }

  const obj = parsed as Record<string, unknown>;

  const schemaVersion = assertNumber(obj["schema_version"], "schema_version");
  if (schemaVersion !== 1) {
    throw new Error(`Unsupported tasks.toml schema_version: ${schemaVersion}`);
  }

  const manifestDigest = assertString(obj["manifest_digest"], "manifest_digest");

  const rawTasks = obj["task"];
  if (!Array.isArray(rawTasks)) {
    throw new Error("Expected [[task]] array in tasks.toml");
  }

  const tasks: TaskEntry[] = [];

  for (const raw of rawTasks) {
    if (typeof raw !== "object" || raw === null) {
      throw new Error("Invalid task entry");
    }
    const r = raw as Record<string, unknown>;

    const op = assertString(r["op"], "op");
    const outputPattern = assertString(r["output_pattern"], "output_pattern");

    // Parse writes_effect if present (triple-quoted JSON string)
    let writesEffect: unknown = undefined;
    if (r["writes_effect"] !== undefined) {
      const effectStr = assertString(r["writes_effect"], "writes_effect");
      try {
        writesEffect = JSON.parse(effectStr.trim());
      } catch (e) {
        throw new Error(`task ${op}: writes_effect is not valid JSON: ${e}`);
      }
    }

    // Parse params
    const rawParams = r["param"];
    const params: TaskParamEntry[] = [];
    if (rawParams !== undefined) {
      if (!Array.isArray(rawParams)) {
        throw new Error(`task ${op}: expected [[task.param]] array`);
      }
      for (const rawParam of rawParams) {
        if (typeof rawParam !== "object" || rawParam === null) {
          throw new Error(`task ${op}: invalid param entry`);
        }
        const p = rawParam as Record<string, unknown>;
        const name = assertString(p["name"], "name");
        const typeVal = p["type"];
        if (!isTaskParamType(typeVal)) {
          throw new Error(`task ${op}: invalid param type: ${typeVal}`);
        }
        const required = assertBoolean(p["required"], "required");
        const nullable = assertBoolean(p["nullable"], "nullable");

        params.push({ name, type: typeVal, required, nullable });
      }
    }

    tasks.push({
      op,
      output_pattern: outputPattern,
      params,
      writes_effect: writesEffect,
    });
  }

  // Sort by op for deterministic output
  tasks.sort((a, b) => a.op.localeCompare(b.op));

  return {
    schema_version: schemaVersion,
    manifest_digest: manifestDigest,
    tasks,
  };
}

function parseCapabilities(tomlPath: string): CapabilityEntry[] {
  const content = readFileSync(tomlPath, "utf-8");
  const parsed: unknown = parseToml(content);

  if (typeof parsed !== "object" || parsed === null) {
    throw new Error("Invalid TOML structure in capabilities.toml");
  }

  const obj = parsed as Record<string, unknown>;

  const schemaVersion = obj["schema_version"];
  if (schemaVersion !== 1) {
    throw new Error(`Unsupported capabilities.toml schema_version: ${schemaVersion}`);
  }

  const rawCaps = obj["capability"];
  if (!Array.isArray(rawCaps)) {
    throw new Error("Expected [[capability]] array in capabilities.toml");
  }

  const entries: CapabilityEntry[] = [];
  const seenIds = new Set<string>();

  for (const raw of rawCaps) {
    if (typeof raw !== "object" || raw === null) {
      throw new Error("Invalid capability entry");
    }
    const r = raw as Record<string, unknown>;

    const id = assertString(r["id"], "id");
    const rfc = assertString(r["rfc"], "rfc");
    const name = assertString(r["name"], "name");
    const statusVal = r["status"];
    if (!isCapabilityStatus(statusVal)) {
      throw new Error(`Invalid capability status: ${statusVal}`);
    }
    const doc = assertString(r["doc"], "doc");

    // Parse payload_schema: string containing JSON or absent
    let payload_schema: JsonSchema | null = null;
    const schemaStr = r["payload_schema"];
    if (schemaStr !== undefined) {
      if (typeof schemaStr !== "string") {
        throw new Error(`capability ${id}: payload_schema must be a string (JSON)"`);
      }
      try {
        payload_schema = JSON.parse(schemaStr) as JsonSchema;
      } catch (e) {
        throw new Error(`capability ${id}: payload_schema is not valid JSON: ${e}`);
      }
      // Enforce object type - non-object payloads not supported
      if (payload_schema.type !== "object") {
        throw new Error(`capability ${id}: payload_schema.type must be "object", got "${payload_schema.type}"`);
      }
    }

    if (seenIds.has(id)) throw new Error(`Duplicate capability id: ${id}`);
    seenIds.add(id);

    entries.push({ id, rfc, name, status: statusVal, doc, payload_schema });
  }

  // Sort by id for deterministic output
  entries.sort((a, b) => a.id.localeCompare(b.id));
  return entries;
}

// ============================================================
// Digest Computation
// ============================================================

function stableStringify(obj: unknown): string {
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

function computeDigest(obj: unknown): string {
  const canonical = stableStringify(obj);
  return createHash("sha256").update(canonical).digest("hex");
}

// ============================================================
// C++ Formatting
// ============================================================

function formatCpp(code: string): string {
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

// ============================================================
// Code Generation
// ============================================================

function generateKeysTs(keys: KeyEntry[], digest: string): string {
  const lines: string[] = [
    "// AUTO-GENERATED - DO NOT EDIT",
    "// Generated by dsl/src/codegen.ts",
    "",
    'export type KeyType = "int" | "float" | "string" | "bool" | "feature_bundle";',
    "",
    "export interface KeyToken {",
    '  readonly kind: "Key";',
    "  readonly id: number;",
    "  readonly name: string;",
    "}",
    "",
    "function key(id: number, name: string): KeyToken {",
    '  return { kind: "Key", id, name };',
    "}",
    "",
    "export const Key = {",
  ];

  for (const k of keys) {
    lines.push(`  ${k.name}: key(${k.key_id}, "${k.name}"),`);
  }

  lines.push("} as const;");
  lines.push("");
  lines.push(`export const KEY_REGISTRY_DIGEST = "${digest}";`);
  lines.push(`export const KEY_COUNT = ${keys.length};`);
  lines.push("");

  return lines.join("\n");
}

function generateParamsTs(params: ParamEntry[], digest: string): string {
  const lines: string[] = [
    "// AUTO-GENERATED - DO NOT EDIT",
    "// Generated by dsl/src/codegen.ts",
    "",
    'export type ParamType = "int" | "float" | "string" | "bool";',
    "",
    "export interface ParamToken {",
    '  readonly kind: "Param";',
    "  readonly id: number;",
    "  readonly name: string;",
    "}",
    "",
    "function param(id: number, name: string): ParamToken {",
    '  return { kind: "Param", id, name };',
    "}",
    "",
    "export const P = {",
  ];

  for (const p of params) {
    lines.push(`  ${p.name}: param(${p.param_id}, "${p.name}"),`);
  }

  lines.push("} as const;");
  lines.push("");
  lines.push(`export const PARAM_REGISTRY_DIGEST = "${digest}";`);
  lines.push(`export const PARAM_COUNT = ${params.length};`);
  lines.push("");

  return lines.join("\n");
}

function generateFeaturesTs(features: FeatureEntry[], digest: string): string {
  const lines: string[] = [
    "// AUTO-GENERATED - DO NOT EDIT",
    "// Generated by dsl/src/codegen.ts",
    "",
    'export type FeatureType = "int" | "float" | "string" | "bool";',
    "",
    "export interface FeatureToken {",
    '  readonly kind: "Feature";',
    "  readonly id: number;",
    "  readonly stage: string;",
    "  readonly name: string;",
    "}",
    "",
    "function feat(id: number, stage: string, name: string): FeatureToken {",
    '  return { kind: "Feature", id, stage, name };',
    "}",
    "",
  ];

  // Group by stage
  const byStage = new Map<string, FeatureEntry[]>();
  for (const f of features) {
    if (!byStage.has(f.stage)) byStage.set(f.stage, []);
    byStage.get(f.stage)!.push(f);
  }

  const stages = Array.from(byStage.keys()).sort();

  lines.push("export const Feat = {");
  for (const stage of stages) {
    const feats = byStage.get(stage)!;
    lines.push(`  ${stage}: {`);
    for (const f of feats) {
      lines.push(`    ${f.name}: feat(${f.feature_id}, "${f.stage}", "${f.name}"),`);
    }
    lines.push("  },");
  }
  lines.push("} as const;");
  lines.push("");
  lines.push(`export const FEATURE_REGISTRY_DIGEST = "${digest}";`);
  lines.push(`export const FEATURE_COUNT = ${features.length};`);
  lines.push("");

  return lines.join("\n");
}

function generateIndexTs(): string {
  return [
    "// AUTO-GENERATED - DO NOT EDIT",
    "// Generated by dsl/src/codegen.ts",
    "",
    'export { Key, KeyToken, KeyType, KEY_REGISTRY_DIGEST, KEY_COUNT } from "./keys.js";',
    'export { P, ParamToken, ParamType, PARAM_REGISTRY_DIGEST, PARAM_COUNT } from "./params.js";',
    'export { Feat, FeatureToken, FeatureType, FEATURE_REGISTRY_DIGEST, FEATURE_COUNT } from "./features.js";',
    'export * from "./validation.js";',
    'export * from "./capabilities.js";',
    'export * from "./tasks.js";',
    'export * from "./task-impl.js";',
    "",
  ].join("\n");
}

/**
 * Generate TypeScript task option interfaces from TaskRegistry.
 *
 * Param mapping:
 *   C++ name    C++ type   TS name    TS type
 *   out_key     Int        outKey     KeyToken
 *   expr_id     ExprId     expr       ExprInput
 *   pred_id     PredId     pred       PredNode
 *   fanout      Int        fanout     number
 *   count       Int        count      number
 *   trace       String     trace      string | undefined
 */
function generateTasksTs(registry: TaskRegistry): string {
  const lines: string[] = [
    "// AUTO-GENERATED from C++ TaskSpec - DO NOT EDIT",
    "// Generated by dsl/src/codegen.ts from registry/tasks.toml",
    '// Regenerate with: pnpm -C dsl run gen',
    "",
    'import type { KeyToken } from "./keys.js";',
    "",
    "// ============================================================",
    "// Expression types (narrow discriminated unions)",
    "// ============================================================",
    "",
    "/** ExprNode - matches engine's ExprIR format (builder-style) */",
    "export type ExprNode =",
    '  | { op: "const_number"; value: number }',
    '  | { op: "const_null" }',
    '  | { op: "key_ref"; key_id: number }',
    '  | { op: "param_ref"; param_id: number }',
    '  | { op: "add"; a: ExprNode; b: ExprNode }',
    '  | { op: "sub"; a: ExprNode; b: ExprNode }',
    '  | { op: "mul"; a: ExprNode; b: ExprNode }',
    '  | { op: "neg"; x: ExprNode }',
    '  | { op: "coalesce"; a: ExprNode; b: ExprNode };',
    "",
    "/** ExprPlaceholder - compile-time placeholder for natural expression syntax */",
    "export interface ExprPlaceholder {",
    "  __expr_id: number;",
    "}",
    "",
    "/** ExprInput - expression input type for tasks (builder or natural syntax) */",
    "export type ExprInput = ExprNode | ExprPlaceholder;",
    "",
    "// ============================================================",
    "// Predicate types (narrow discriminated unions)",
    "// ============================================================",
    "",
    "/** Regex pattern - literal string or param reference */",
    "export type RegexPattern =",
    '  | { kind: "literal"; value: string }',
    '  | { kind: "param"; param_id: number };',
    "",
    "/** PredNode - matches engine's PredIR format */",
    "export type PredNode =",
    '  | { op: "const_bool"; value: boolean }',
    '  | { op: "and"; a: PredNode; b: PredNode }',
    '  | { op: "or"; a: PredNode; b: PredNode }',
    '  | { op: "not"; x: PredNode }',
    '  | { op: "cmp"; cmp: "==" | "!=" | "<" | "<=" | ">" | ">="; a: ExprNode; b: ExprNode }',
    '  | { op: "in"; lhs: ExprNode; list: (number | string)[] }',
    '  | { op: "is_null"; x: ExprNode }',
    '  | { op: "not_null"; x: ExprNode }',
    '  | { op: "regex"; key_id: number; pattern: RegexPattern; flags: string };',
    "",
  ];

  // Classify tasks: viewer.* are source tasks, others are transform tasks
  const sourceTasks: TaskEntry[] = [];
  const transformTasks: TaskEntry[] = [];

  for (const task of registry.tasks) {
    if (task.op.startsWith("viewer.")) {
      sourceTasks.push(task);
    } else {
      transformTasks.push(task);
    }
  }

  // Generate source task option interfaces
  if (sourceTasks.length > 0) {
    lines.push("// ============================================================");
    lines.push("// Source task option interfaces");
    lines.push("// ============================================================");
    lines.push("");

    for (const task of sourceTasks) {
      const interfaceName = opToInterfaceName(task.op);
      lines.push(`export interface ${interfaceName} {`);
      for (const param of task.params) {
        const tsName = cppNameToTsName(param.name);
        const tsType = paramTypeToTsType(param);
        const optional = !param.required ? "?" : "";
        lines.push(`  ${tsName}${optional}: ${tsType};`);
      }
      // All tasks support extensions
      lines.push("  extensions?: Record<string, unknown>;");
      lines.push("}");
      lines.push("");
    }
  }

  // Generate transform task option interfaces
  if (transformTasks.length > 0) {
    lines.push("// ============================================================");
    lines.push("// Transform task option interfaces");
    lines.push("// ============================================================");
    lines.push("");

    for (const task of transformTasks) {
      const interfaceName = opToInterfaceName(task.op);
      lines.push(`export interface ${interfaceName} {`);
      for (const param of task.params) {
        const tsName = cppNameToTsName(param.name);
        const tsType = paramTypeToTsType(param);
        const optional = !param.required ? "?" : "";
        lines.push(`  ${tsName}${optional}: ${tsType};`);
      }
      // All tasks support extensions
      lines.push("  extensions?: Record<string, unknown>;");
      lines.push("}");
      lines.push("");
    }
  }

  // Export metadata
  lines.push("// ============================================================");
  lines.push("// Metadata");
  lines.push("// ============================================================");
  lines.push("");
  lines.push(`export const TASK_MANIFEST_DIGEST = "${registry.manifest_digest}";`);
  lines.push(`export const TASK_COUNT = ${registry.tasks.length};`);
  lines.push("");

  return lines.join("\n");
}

/**
 * Convert C++ param name to TypeScript name (snake_case to camelCase)
 * Special cases: out_key -> outKey, expr_id -> expr, pred_id -> pred
 */
function cppNameToTsName(cppName: string): string {
  // Special mappings
  if (cppName === "expr_id") return "expr";
  if (cppName === "pred_id") return "pred";

  // Convert snake_case to camelCase
  return cppName.replace(/_([a-z])/g, (_, c) => c.toUpperCase());
}

/**
 * Convert C++ op name to TypeScript interface name
 * e.g., "viewer.follow" -> "ViewerFollowOpts"
 *       "vm" -> "VmOpts"
 */
function opToInterfaceName(op: string): string {
  const parts = op.split(".");
  const pascalParts = parts.map(p =>
    p.replace(/(^|_)([a-z])/g, (_, __, c) => c.toUpperCase())
  );
  return pascalParts.join("") + "Opts";
}

/**
 * Convert C++ param type to TypeScript type
 *
 * Handles nullable params: if nullable=true, emit "baseType | null"
 * This maintains parity with C++ validation which accepts null for nullable params.
 */
function paramTypeToTsType(param: TaskParamEntry): string {
  const { type, nullable } = param;

  let baseType: string;
  switch (type) {
    case "int":
      // Special case: out_key is KeyToken
      if (param.name === "out_key") {
        baseType = "KeyToken";
      } else {
        baseType = "number";
      }
      break;
    case "float":
      baseType = "number";
      break;
    case "bool":
      baseType = "boolean";
      break;
    case "string":
      baseType = "string";
      break;
    case "expr_id":
      baseType = "ExprInput";
      break;
    case "pred_id":
      baseType = "PredNode";
      break;
    default:
      baseType = "unknown";
  }

  // Nullable types: add " | null" to maintain parity with C++ validation
  if (nullable) {
    return `${baseType} | null`;
  }

  return baseType;
}

/**
 * Get input arity for a task op.
 * - viewer.*: 0 (source task, no inputs)
 * - concat: 2 (binary, lhs and rhs)
 * - others: 1 (unary transform)
 */
function getTaskArity(op: string): number {
  if (op.startsWith("viewer.")) return 0;
  if (op === "concat") return 2;
  return 1;
}

/**
 * Convert op name to method name.
 * e.g., "viewer.follow" -> "follow"
 *       "vm" -> "vm"
 */
function opToMethodName(op: string): string {
  // For viewer.* ops, return the part after the dot
  if (op.startsWith("viewer.")) {
    return op.slice("viewer.".length);
  }
  return op;
}

/**
 * Generate task implementation functions.
 * These are used by CandidateSet and PlanCtx.viewer methods.
 */
function generateTaskImplTs(registry: TaskRegistry): string {
  const lines: string[] = [
    "// AUTO-GENERATED from C++ TaskSpec - DO NOT EDIT",
    "// Generated by dsl/src/codegen.ts from registry/tasks.toml",
    '// Regenerate with: pnpm -C dsl run gen',
    "",
    '// This file contains the generated task implementations.',
    '// It is used by plan.ts to implement task methods without manual code.',
    "",
    'import type { ExprNode, ExprPlaceholder, ExprInput, PredNode } from "./tasks.js";',
    'import type { KeyToken } from "./keys.js";',
    "",
    "// ============================================================",
    "// Types for plan context interface",
    "// ============================================================",
    "",
    "/** Interface for expression placeholder detection */",
    "export function isExprPlaceholder(value: unknown): value is ExprPlaceholder {",
    "  return (",
    '    value !== null &&',
    '    typeof value === "object" &&',
    '    "__expr_id" in value &&',
    "    typeof (value as ExprPlaceholder).__expr_id === \"number\"",
    "  );",
    "}",
    "",
    "/** Interface that PlanCtx must implement for task impls to use */",
    "export interface TaskContext {",
    "  addNode(",
    "    op: string,",
    "    inputs: string[],",
    "    params: Record<string, unknown>,",
    "    extensions?: Record<string, unknown>",
    "  ): string;",
    "  addExpr(expr: ExprNode): string;",
    "  addPred(pred: PredNode): string;",
    "}",
    "",
    "// ============================================================",
    "// Validation helpers",
    "// ============================================================",
    "",
    "function assertNotUndefined<T>(value: T | undefined, name: string): asserts value is T {",
    '  if (value === undefined) throw new Error(`${name} is undefined`);',
    "}",
    "",
    "function assertInteger(value: unknown, name: string): asserts value is number {",
    "  if (typeof value !== \"number\" || !Number.isInteger(value)) {",
    "    throw new Error(`${name} must be an integer, got ${typeof value === \"number\" ? value : typeof value}`);",
    "  }",
    "}",
    "",
    "function assertKeyToken(value: unknown, name: string): void {",
    "  if (value === null || typeof value !== \"object\") {",
    "    throw new Error(`${name} must be a KeyToken, got ${value === null ? \"null\" : typeof value}`);",
    "  }",
    "  const token = value as Record<string, unknown>;",
    "  if (typeof token.id !== \"number\" || !Number.isInteger(token.id)) {",
    "    throw new Error(`${name}.id must be an integer, got ${typeof token.id === \"number\" ? token.id : typeof token.id}`);",
    "  }",
    "}",
    "",
    "function checkNoUndefined(obj: Record<string, unknown>, context: string): void {",
    "  for (const [key, value] of Object.entries(obj)) {",
    "    if (value === undefined) {",
    "      throw new Error(`${context}.${key} is undefined`);",
    "    }",
    "    // Recursively check nested objects (but not arrays)",
    "    if (typeof value === \"object\" && value !== null && !Array.isArray(value)) {",
    "      checkNoUndefined(value as Record<string, unknown>, `${context}.${key}`);",
    "    }",
    "  }",
    "}",
    "",
  ];

  // Classify tasks
  const sourceTasks = registry.tasks.filter(t => t.op.startsWith("viewer."));
  const transformTasks = registry.tasks.filter(t => !t.op.startsWith("viewer."));

  // Generate source task implementations
  if (sourceTasks.length > 0) {
    lines.push("// ============================================================");
    lines.push("// Source task implementations (for PlanCtx.viewer)");
    lines.push("// ============================================================");
    lines.push("");

    for (const task of sourceTasks) {
      const methodName = opToMethodName(task.op);

      lines.push(`/** Implementation for ${task.op} */`);
      lines.push(`export function ${methodName}Impl(`);
      lines.push(`  ctx: TaskContext,`);
      lines.push(`  opts: {`);

      // Generate opts type inline (same as interface but with extensions)
      for (const param of task.params) {
        const tsName = cppNameToTsName(param.name);
        const tsType = paramTypeToTsType(param);
        const optional = !param.required ? "?" : "";
        lines.push(`    ${tsName}${optional}: ${tsType};`);
      }
      lines.push(`    extensions?: Record<string, unknown>;`);
      lines.push(`  }`);
      lines.push(`): string {`);

      // Generate validation
      lines.push(`  assertNotUndefined(opts, "${methodName}(opts)");`);
      for (const param of task.params) {
        if (param.required) {
          const tsName = cppNameToTsName(param.name);
          lines.push(`  assertNotUndefined(opts.${tsName}, "${methodName}({ ${tsName} })");`);
          // Add type-specific validation
          if (param.type === "int") {
            lines.push(`  assertInteger(opts.${tsName}, "${methodName}({ ${tsName} })");`);
          }
        }
      }
      lines.push(`  const { extensions, ...rest } = opts;`);
      lines.push(`  checkNoUndefined(rest as Record<string, unknown>, "${methodName}(opts)");`);
      lines.push("");

      // Generate params object
      lines.push(`  const params: Record<string, unknown> = {`);
      for (const param of task.params) {
        const tsName = cppNameToTsName(param.name);
        const cppName = param.name;

        // Handle trace default
        if (param.name === "trace") {
          lines.push(`    ${cppName}: opts.${tsName} ?? null,`);
        } else {
          lines.push(`    ${cppName}: opts.${tsName},`);
        }
      }
      lines.push(`  };`);
      lines.push("");

      // Create node (source tasks have no inputs)
      lines.push(`  return ctx.addNode("${task.op}", [], params, extensions);`);
      lines.push(`}`);
      lines.push("");
    }
  }

  // Generate transform task implementations
  if (transformTasks.length > 0) {
    lines.push("// ============================================================");
    lines.push("// Transform task implementations (for CandidateSet methods)");
    lines.push("// ============================================================");
    lines.push("");

    for (const task of transformTasks) {
      const methodName = opToMethodName(task.op);
      const arity = getTaskArity(task.op);

      lines.push(`/** Implementation for ${task.op} */`);

      if (arity === 2) {
        // Binary task (concat)
        lines.push(`export function ${methodName}Impl(`);
        lines.push(`  ctx: TaskContext,`);
        lines.push(`  lhsNodeId: string,`);
        lines.push(`  rhsNodeId: string,`);
        lines.push(`  opts?: {`);
      } else {
        // Unary task
        lines.push(`export function ${methodName}Impl(`);
        lines.push(`  ctx: TaskContext,`);
        lines.push(`  inputNodeId: string,`);
        lines.push(`  opts: {`);
      }

      // Generate opts type inline
      for (const param of task.params) {
        const tsName = cppNameToTsName(param.name);
        const tsType = paramTypeToTsType(param);
        const optional = !param.required ? "?" : "";
        lines.push(`    ${tsName}${optional}: ${tsType};`);
      }
      lines.push(`    extensions?: Record<string, unknown>;`);
      lines.push(`  }`);
      lines.push(`): string {`);

      // Generate validation
      if (arity === 2) {
        // Binary task - opts is optional
        lines.push(`  let extensions: Record<string, unknown> | undefined;`);
        lines.push(`  if (opts !== undefined) {`);
        lines.push(`    const { extensions: ext, ...rest } = opts;`);
        lines.push(`    extensions = ext;`);
        lines.push(`    checkNoUndefined(rest as Record<string, unknown>, "${methodName}(opts)");`);
        lines.push(`  }`);
      } else {
        // Unary task
        lines.push(`  assertNotUndefined(opts, "${methodName}(opts)");`);
        for (const param of task.params) {
          if (param.required) {
            const tsName = cppNameToTsName(param.name);
            lines.push(`  assertNotUndefined(opts.${tsName}, "${methodName}({ ${tsName} })");`);
            // Add type-specific validation
            if (param.name === "out_key") {
              // out_key is a KeyToken, validate it has valid .id
              lines.push(`  assertKeyToken(opts.${tsName}, "${methodName}({ ${tsName} })");`);
            } else if (param.type === "int") {
              lines.push(`  assertInteger(opts.${tsName}, "${methodName}({ ${tsName} })");`);
            }
          }
        }
        lines.push(`  const { extensions, ...rest } = opts;`);
        lines.push(`  checkNoUndefined(rest as Record<string, unknown>, "${methodName}(opts)");`);
      }
      lines.push("");

      // Handle expr_id and pred_id params (table management)
      const hasExprId = task.params.some(p => p.type === "expr_id");
      const hasPredId = task.params.some(p => p.type === "pred_id");

      if (hasExprId) {
        lines.push(`  // Handle expression table`);
        lines.push(`  let exprId: string;`);
        lines.push(`  if (isExprPlaceholder(opts.expr)) {`);
        lines.push(`    // AST-extracted expression - use special prefix for later remapping`);
        lines.push(`    exprId = \`__static_e\${opts.expr.__expr_id}\`;`);
        lines.push(`  } else {`);
        lines.push(`    // Regular builder-style expression`);
        lines.push(`    exprId = ctx.addExpr(opts.expr as ExprNode);`);
        lines.push(`  }`);
        lines.push("");
      }

      if (hasPredId) {
        lines.push(`  // Handle predicate table`);
        lines.push(`  const predId = ctx.addPred(opts.pred);`);
        lines.push("");
      }

      // Generate params object
      if (arity === 2) {
        lines.push(`  const params: Record<string, unknown> = {`);
        lines.push(`    trace: opts?.trace ?? null,`);
        lines.push(`  };`);
      } else {
        lines.push(`  const params: Record<string, unknown> = {`);
        for (const param of task.params) {
          const tsName = cppNameToTsName(param.name);
          const cppName = param.name;

          if (param.type === "expr_id") {
            lines.push(`    ${cppName}: exprId,`);
          } else if (param.type === "pred_id") {
            lines.push(`    ${cppName}: predId,`);
          } else if (param.name === "out_key") {
            // KeyToken - extract .id
            lines.push(`    ${cppName}: opts.${tsName}.id,`);
          } else if (param.name === "trace") {
            lines.push(`    ${cppName}: opts.${tsName} ?? null,`);
          } else {
            lines.push(`    ${cppName}: opts.${tsName},`);
          }
        }
        lines.push(`  };`);
      }
      lines.push("");

      // Create node
      if (arity === 2) {
        lines.push(`  return ctx.addNode("${task.op}", [lhsNodeId, rhsNodeId], params, extensions);`);
      } else {
        lines.push(`  return ctx.addNode("${task.op}", [inputNodeId], params, extensions);`);
      }
      lines.push(`}`);
      lines.push("");
    }
  }

  // Export task registry for plan.ts to use
  lines.push("// ============================================================");
  lines.push("// Task metadata for runtime use");
  lines.push("// ============================================================");
  lines.push("");
  lines.push("export const GENERATED_TASKS = {");
  lines.push("  source: [" + sourceTasks.map(t => `"${opToMethodName(t.op)}"`).join(", ") + "],");
  lines.push("  transform: [" + transformTasks.map(t => `"${opToMethodName(t.op)}"`).join(", ") + "],");
  lines.push("} as const;");
  lines.push("");

  return lines.join("\n");
}

function generatePlanGlobalsDts(): string {
  return [
    "// AUTO-GENERATED - DO NOT EDIT",
    "// Generated by dsl/src/codegen.ts",
    "//",
    "// Global type declarations for plan authoring.",
    "// Key, P, and coalesce are injected as globals by the dslc compiler.",
    "// This file provides TypeScript type information for editor support.",
    "",
    'import type { Key as KeyType } from "@ranking-dsl/generated";',
    'import type { P as PType } from "@ranking-dsl/generated";',
    'import type { coalesce as coalesceType } from "@ranking-dsl/runtime";',
    "",
    "declare global {",
    "  /**",
    "   * Key registry - column references for vm/filter expressions.",
    "   * Injected by dslc compiler.",
    "   */",
    "  const Key: typeof KeyType;",
    "",
    "  /**",
    "   * Param registry - runtime parameter references.",
    "   * Injected by dslc compiler.",
    "   */",
    "  const P: typeof PType;",
    "",
    "  /**",
    "   * Coalesce function for null handling in natural expressions.",
    "   * Usage: Key.score * coalesce(P.weight, 0.2)",
    "   * Injected by dslc compiler.",
    "   */",
    "  const coalesce: typeof coalesceType;",
    "}",
    "",
    "export {};",
    "",
  ].join("\n");
}

function cppType(t: string): string {
  switch (t) {
    case "int": return "Int";
    case "float": return "Float";
    case "string": return "String";
    case "bool": return "Bool";
    case "feature_bundle": return "FeatureBundle";
    default: return "Unknown";
  }
}

function cppStatus(s: Status): string {
  switch (s) {
    case "active": return "Active";
    case "deprecated": return "Deprecated";
    case "blocked": return "Blocked";
  }
}

function generateKeysH(keys: KeyEntry[], digest: string): string {
  const lines: string[] = [
    "// AUTO-GENERATED - DO NOT EDIT",
    "// Generated by dsl/src/codegen.ts",
    "#pragma once",
    "",
    "#include <cstdint>",
    "#include <string_view>",
    "#include <array>",
    "",
    "namespace rankd {",
    "",
    "enum class KeyId : uint32_t {",
  ];

  for (const k of keys) {
    lines.push(`    ${k.name} = ${k.key_id},`);
  }

  lines.push("};");
  lines.push("");
  lines.push("// Helper to convert KeyId to uint32_t (avoids verbose static_cast)");
  lines.push("constexpr uint32_t key_id(KeyId k) noexcept {");
  lines.push("  return static_cast<uint32_t>(k);");
  lines.push("}");
  lines.push("");
  lines.push("enum class KeyType { Int, Float, String, Bool, FeatureBundle };");
  lines.push("enum class Status { Active, Deprecated, Blocked };")
  lines.push("");
  lines.push("struct KeyMeta {");
  lines.push("    uint32_t id;");
  lines.push("    std::string_view name;");
  lines.push("    KeyType type;");
  lines.push("    bool nullable;");
  lines.push("    Status status;");
  lines.push("    bool allow_read;");
  lines.push("    bool allow_write;");
  lines.push("    uint32_t replaced_by;  // 0 if none");
  lines.push("    bool has_default;");
  lines.push("    std::string_view default_json;");
  lines.push("};");
  lines.push("");
  lines.push(`inline constexpr size_t kKeyCount = ${keys.length};`);
  lines.push(`inline constexpr std::string_view kKeyRegistryDigest = "${digest}";`);
  lines.push("");
  lines.push("inline constexpr std::array<KeyMeta, kKeyCount> kKeyRegistry = {{");

  for (const k of keys) {
    // Double-stringify: first creates valid JSON, second wraps it as a C++ string literal
    // e.g., number 0 → JSON "0" → C++ literal "0"
    // e.g., string "US" → JSON "\"US\"" → C++ literal "\"US\""
    const defaultJson = k.default !== null ? JSON.stringify(JSON.stringify(k.default)) : '""';
    lines.push(`    {${k.key_id}, "${k.name}", KeyType::${cppType(k.type)}, ${k.nullable}, Status::${cppStatus(k.status)}, ${k.allow_read}, ${k.allow_write}, ${k.replaced_by ?? 0}, ${k.default !== null}, ${defaultJson}},`);
  }

  lines.push("}};");
  lines.push("");
  lines.push("} // namespace rankd");
  lines.push("");

  return lines.join("\n");
}

function generateParamsH(params: ParamEntry[], digest: string): string {
  const lines: string[] = [
    "// AUTO-GENERATED - DO NOT EDIT",
    "// Generated by dsl/src/codegen.ts",
    "#pragma once",
    "",
    "#include <array>",
    "#include <cstdint>",
    "#include <string_view>",
    "",
    '#include "key_registry.h"  // for Status',
    "",
    "namespace rankd {",
    "",
    "enum class ParamId : uint32_t {",
  ];

  for (const p of params) {
    lines.push(`    ${p.name} = ${p.param_id},`);
  }

  lines.push("};");
  lines.push("");
  lines.push("enum class ParamType { Int, Float, String, Bool };");
  lines.push("");
  lines.push("struct ParamMeta {");
  lines.push("    uint32_t id;");
  lines.push("    std::string_view name;");
  lines.push("    ParamType type;");
  lines.push("    bool nullable;");
  lines.push("    Status status;");
  lines.push("    bool allow_read;");
  lines.push("    bool allow_write;");
  lines.push("    uint32_t replaced_by;  // 0 if none");
  lines.push("};");
  lines.push("");
  lines.push(`inline constexpr size_t kParamCount = ${params.length};`);
  lines.push(`inline constexpr std::string_view kParamRegistryDigest = "${digest}";`);
  lines.push("");
  lines.push("inline constexpr std::array<ParamMeta, kParamCount> kParamRegistry = {{");

  for (const p of params) {
    lines.push(`    {${p.param_id}, "${p.name}", ParamType::${cppType(p.type)}, ${p.nullable}, Status::${cppStatus(p.status)}, ${p.allow_read}, ${p.allow_write}, ${p.replaced_by ?? 0}},`);
  }

  lines.push("}};");
  lines.push("");
  lines.push("} // namespace rankd");
  lines.push("");

  return lines.join("\n");
}

function generateFeaturesH(features: FeatureEntry[], digest: string): string {
  const lines: string[] = [
    "// AUTO-GENERATED - DO NOT EDIT",
    "// Generated by dsl/src/codegen.ts",
    "#pragma once",
    "",
    "#include <array>",
    "#include <cstdint>",
    "#include <string_view>",
    "",
    '#include "key_registry.h"  // for Status',
    "",
    "namespace rankd {",
    "",
    "enum class FeatureId : uint32_t {",
  ];

  for (const f of features) {
    lines.push(`    ${f.stage}_${f.name} = ${f.feature_id},`);
  }

  lines.push("};");
  lines.push("");
  lines.push("enum class FeatureType { Int, Float, String, Bool };");
  lines.push("");
  lines.push("struct FeatureMeta {");
  lines.push("    uint32_t id;");
  lines.push("    std::string_view stage;");
  lines.push("    std::string_view name;");
  lines.push("    FeatureType type;");
  lines.push("    bool nullable;");
  lines.push("    Status status;");
  lines.push("};");
  lines.push("");
  lines.push(`inline constexpr size_t kFeatureCount = ${features.length};`);
  lines.push(`inline constexpr std::string_view kFeatureRegistryDigest = "${digest}";`);
  lines.push("");
  lines.push("inline constexpr std::array<FeatureMeta, kFeatureCount> kFeatureRegistry = {{");

  for (const f of features) {
    lines.push(`    {${f.feature_id}, "${f.stage}", "${f.name}", FeatureType::${cppType(f.type)}, ${f.nullable}, Status::${cppStatus(f.status)}},`);
  }

  lines.push("}};");
  lines.push("");
  lines.push("} // namespace rankd");
  lines.push("");

  return lines.join("\n");
}

function generateValidationTs(validation: ValidationRules, digest: string): string {
  const lines: string[] = [
    "// AUTO-GENERATED - DO NOT EDIT",
    "// Generated by dsl/src/codegen.ts from registry/validation.toml",
    "",
    "// ============================================================",
    "// Patterns (as RegExp)",
    "// ============================================================",
    "",
  ];

  for (const [name, pattern] of Object.entries(validation.patterns)) {
    // Use new RegExp() to avoid escaping issues with regex literal syntax
    lines.push(`export const ${name.toUpperCase()}_PATTERN = new RegExp(${JSON.stringify(pattern)});`);
  }

  lines.push("");
  lines.push("// ============================================================");
  lines.push("// Validation functions");
  lines.push("// ============================================================");
  lines.push("");

  for (const name of Object.keys(validation.patterns)) {
    const fnName = `isValid${name.split("_").map(w => w[0].toUpperCase() + w.slice(1)).join("")}`;
    lines.push(`export function ${fnName}(value: string): boolean {`);
    lines.push(`  return ${name.toUpperCase()}_PATTERN.test(value);`);
    lines.push("}");
    lines.push("");
  }

  lines.push("// ============================================================");
  lines.push("// Limits");
  lines.push("// ============================================================");
  lines.push("");

  for (const [name, value] of Object.entries(validation.limits)) {
    lines.push(`export const ${name.toUpperCase()} = ${value};`);
  }

  lines.push("");
  lines.push("// ============================================================");
  lines.push("// Enums");
  lines.push("// ============================================================");
  lines.push("");

  for (const [name, values] of Object.entries(validation.enums)) {
    const typeName = name.split("_").map(w => w[0].toUpperCase() + w.slice(1)).join("");
    lines.push(`export const ${name.toUpperCase()} = [${values.map(v => JSON.stringify(v)).join(", ")}] as const;`);
    lines.push(`export type ${typeName} = typeof ${name.toUpperCase()}[number];`);
    lines.push("");
  }

  lines.push(`export const VALIDATION_DIGEST = "${digest}";`);
  lines.push("");

  return lines.join("\n");
}

function generateValidationH(validation: ValidationRules, digest: string): string {
  const lines: string[] = [
    "// AUTO-GENERATED - DO NOT EDIT",
    "// Generated by dsl/src/codegen.ts from registry/validation.toml",
    "#pragma once",
    "",
    "#include <array>",
    "#include <cstdint>",
    "#include <regex>",
    "#include <string>",
    "#include <string_view>",
    "",
    "namespace rankd {",
    "namespace validation {",
    "",
    "// ============================================================",
    "// Patterns",
    "// ============================================================",
    "",
  ];

  for (const [name, pattern] of Object.entries(validation.patterns)) {
    // Escape backslashes and quotes for C++ string literal
    const escapedPattern = pattern.replace(/\\/g, "\\\\").replace(/"/g, '\\"');
    lines.push(`inline const std::string k${name.split("_").map(w => w[0].toUpperCase() + w.slice(1)).join("")}Pattern = "${escapedPattern}";`);
  }

  lines.push("");
  lines.push("// ============================================================");
  lines.push("// Validation functions");
  lines.push("// ============================================================");
  lines.push("");

  for (const [name] of Object.entries(validation.patterns)) {
    const fnName = `is_valid_${name}`;
    const patternVar = `k${name.split("_").map(w => w[0].toUpperCase() + w.slice(1)).join("")}Pattern`;
    lines.push(`inline bool ${fnName}(const std::string& value) {`);
    lines.push(`    static const std::regex pattern(${patternVar});`);
    lines.push(`    return std::regex_match(value, pattern);`);
    lines.push("}");
    lines.push("");
  }

  lines.push("// ============================================================");
  lines.push("// Limits");
  lines.push("// ============================================================");
  lines.push("");

  for (const [name, value] of Object.entries(validation.limits)) {
    const constName = `k${name.split("_").map(w => w[0].toUpperCase() + w.slice(1)).join("")}`;
    // Use appropriate type based on value size
    const cppType = value > 2147483647 ? "uint64_t" : "uint32_t";
    lines.push(`inline constexpr ${cppType} ${constName} = ${value};`);
  }

  lines.push("");
  lines.push("// ============================================================");
  lines.push("// Enums");
  lines.push("// ============================================================");
  lines.push("");

  for (const [name, values] of Object.entries(validation.enums)) {
    const constName = `k${name.split("_").map(w => w[0].toUpperCase() + w.slice(1)).join("")}`;
    lines.push(`inline constexpr std::array<std::string_view, ${values.length}> ${constName} = {{`);
    for (const v of values) {
      lines.push(`    ${JSON.stringify(v)},`);
    }
    lines.push("}};");
    lines.push("");
  }

  lines.push(`inline constexpr std::string_view kValidationDigest = "${digest}";`);
  lines.push("");
  lines.push("} // namespace validation");
  lines.push("} // namespace rankd");
  lines.push("");

  return lines.join("\n");
}

// ============================================================
// Capabilities Code Generation
// ============================================================

function generateCapabilitiesTs(capabilities: CapabilityEntry[], digest: string): string {
  const lines: string[] = [
    "// AUTO-GENERATED - DO NOT EDIT",
    "// Generated by dsl/src/codegen.ts from registry/capabilities.toml",
    "",
    'export type CapabilityStatus = "implemented" | "draft" | "deprecated" | "blocked";',
    "",
    "// JSON Schema subset for capability payload validation",
    "export interface JsonSchema {",
    "  type?: string;",
    "  properties?: Record<string, JsonSchema>;",
    "  additionalProperties?: boolean;",
    "  required?: string[];",
    "}",
    "",
    "export interface CapabilityMeta {",
    "  id: string;",
    "  rfc: string;",
    "  name: string;",
    "  status: CapabilityStatus;",
    "  doc: string;",
    "  payloadSchema: JsonSchema | null;",
    "}",
    "",
    "export const CAPABILITY_REGISTRY: Record<string, CapabilityMeta> = {",
  ];

  for (const cap of capabilities) {
    const schemaStr = cap.payload_schema === null
      ? "null"
      : JSON.stringify(cap.payload_schema);
    lines.push(`  ${JSON.stringify(cap.id)}: {`);
    lines.push(`    id: ${JSON.stringify(cap.id)},`);
    lines.push(`    rfc: ${JSON.stringify(cap.rfc)},`);
    lines.push(`    name: ${JSON.stringify(cap.name)},`);
    lines.push(`    status: ${JSON.stringify(cap.status)},`);
    lines.push(`    doc: ${JSON.stringify(cap.doc)},`);
    lines.push(`    payloadSchema: ${schemaStr},`);
    lines.push("  },");
  }

  lines.push("};");
  lines.push("");
  lines.push("export const SUPPORTED_CAPABILITIES = new Set(");
  lines.push("  Object.entries(CAPABILITY_REGISTRY)");
  lines.push('    .filter(([, meta]) => meta.status === "implemented" || meta.status === "deprecated")');
  lines.push("    .map(([id]) => id)");
  lines.push(");");
  lines.push("");
  lines.push(`export const CAPABILITY_REGISTRY_DIGEST = "${digest}";`);
  lines.push(`export const CAPABILITY_COUNT = ${capabilities.length};`);
  lines.push("");
  lines.push("// ============================================================");
  lines.push("// Simple JSON Schema validator (subset for capability payloads)");
  lines.push("// ============================================================");
  lines.push("");
  lines.push("export function validatePayload(capId: string, payload: unknown): void {");
  lines.push("  const meta = CAPABILITY_REGISTRY[capId];");
  lines.push("  if (!meta) return; // Unknown cap handled elsewhere");
  lines.push("");
  lines.push("  const schema = meta.payloadSchema;");
  lines.push("");
  lines.push("  // No schema = no payload allowed");
  lines.push("  if (schema === null) {");
  lines.push("    if (payload !== undefined && payload !== null) {");
  lines.push("      throw new Error(`capability '${capId}': no payload allowed`);");
  lines.push("    }");
  lines.push("    return;");
  lines.push("  }");
  lines.push("");
  lines.push("  // Undefined payload is OK (key omitted), but null is invalid for object schemas");
  lines.push("  if (payload === undefined) return;");
  lines.push("  if (payload === null) {");
  lines.push("    throw new Error(`capability '${capId}': payload is null, expected object (use {} or omit key)`);");
  lines.push("  }");
  lines.push("");
  lines.push('  if (schema.type === "object") {');
  lines.push("    if (typeof payload !== \"object\" || payload === null || Array.isArray(payload)) {");
  lines.push("      throw new Error(`capability '${capId}': payload must be object`);");
  lines.push("    }");
  lines.push("    const obj = payload as Record<string, unknown>;");
  lines.push("    if (schema.additionalProperties === false) {");
  lines.push("      const allowed = new Set(Object.keys(schema.properties ?? {}));");
  lines.push("      for (const key of Object.keys(obj)) {");
  lines.push("        if (!allowed.has(key)) {");
  lines.push("          throw new Error(`capability '${capId}': unexpected key '${key}'`);");
  lines.push("        }");
  lines.push("      }");
  lines.push("    }");
  lines.push("    // Check required properties");
  lines.push("    for (const req of schema.required ?? []) {");
  lines.push("      if (!(req in obj)) {");
  lines.push("        throw new Error(`capability '${capId}': missing required '${req}'`);");
  lines.push("      }");
  lines.push("    }");
  lines.push("    // Type-check properties");
  lines.push("    for (const [key, propSchema] of Object.entries(schema.properties ?? {})) {");
  lines.push("      if (key in obj) {");
  lines.push("        validatePropertyType(capId, key, obj[key], propSchema);");
  lines.push("      }");
  lines.push("    }");
  lines.push("  }");
  lines.push("}");
  lines.push("");
  lines.push("function validatePropertyType(");
  lines.push("  capId: string,");
  lines.push("  key: string,");
  lines.push("  value: unknown,");
  lines.push("  schema: JsonSchema");
  lines.push("): void {");
  lines.push('  if (schema.type === "boolean" && typeof value !== "boolean") {');
  lines.push("    throw new Error(`capability '${capId}': '${key}' must be boolean`);");
  lines.push("  }");
  lines.push('  if (schema.type === "string" && typeof value !== "string") {');
  lines.push("    throw new Error(`capability '${capId}': '${key}' must be string`);");
  lines.push("  }");
  lines.push('  if (schema.type === "number" && typeof value !== "number") {');
  lines.push("    throw new Error(`capability '${capId}': '${key}' must be number`);");
  lines.push("  }");
  lines.push('  if (schema.type === "array" && !Array.isArray(value)) {');
  lines.push("    throw new Error(`capability '${capId}': '${key}' must be array`);");
  lines.push("  }");
  lines.push('  if (schema.type === "object" && (typeof value !== "object" || value === null || Array.isArray(value))) {');
  lines.push("    throw new Error(`capability '${capId}': '${key}' must be object`);");
  lines.push("  }");
  lines.push("}");
  lines.push("");

  return lines.join("\n");
}

function cppCapabilityStatus(s: CapabilityStatus): string {
  switch (s) {
    case "implemented": return "Implemented";
    case "draft": return "Draft";
    case "deprecated": return "Deprecated";
    case "blocked": return "Blocked";
  }
}

function cppPropertyType(jsonType: string | undefined): string {
  switch (jsonType) {
    case "boolean": return "Boolean";
    case "string": return "String";
    case "number": return "Number";
    case "array": return "Array";
    case "object": return "Object";
    default: return "Unknown";
  }
}

// Sanitize capability id for use as C++ identifier
// Uses full id (including version) to ensure uniqueness across versions
// Replace non-alphanumeric chars with underscore, prefix with _ if starts with digit
function cppCapabilityIdent(id: string): string {
  let ident = id.replace(/[^a-zA-Z0-9]/g, "_");
  if (/^[0-9]/.test(ident)) {
    ident = "_" + ident;
  }
  return ident;
}

function generateCapabilitiesH(capabilities: CapabilityEntry[], digest: string): string {
  const lines: string[] = [
    "// AUTO-GENERATED - DO NOT EDIT",
    "// Generated by dsl/src/codegen.ts from registry/capabilities.toml",
    "#pragma once",
    "",
    "#include <array>",
    "#include <optional>",
    "#include <string_view>",
    "",
    "namespace rankd {",
    "",
    "enum class CapabilityStatus { Implemented, Draft, Deprecated, Blocked };",
    "enum class PropertyType { Boolean, String, Number, Array, Object, Unknown };",
    "",
    "// Property metadata for type checking",
    "struct PropertyMeta {",
    "  std::string_view name;",
    "  PropertyType type;",
    "};",
    "",
    "// Simple schema representation (subset of JSON Schema)",
    "struct PayloadSchema {",
    "  bool has_schema;                          // false = no payload allowed",
    "  bool additional_properties;               // true = allow extra keys",
    "  const std::string_view* allowed_keys;     // pointer to array of allowed property names",
    "  size_t num_allowed_keys;                  // number of allowed properties",
    "  const std::string_view* required_keys;    // pointer to array of required property names",
    "  size_t num_required_keys;                 // number of required properties",
    "  const PropertyMeta* property_types;       // pointer to array of property type info",
    "  size_t num_property_types;                // number of property type entries",
    "};",
    "",
    "struct CapabilityMeta {",
    "  std::string_view id;",
    "  std::string_view rfc;",
    "  std::string_view name;",
    "  CapabilityStatus status;",
    "  std::string_view doc;",
    "  PayloadSchema schema;",
    "};",
    "",
  ];

  // Generate arrays for each capability
  for (const cap of capabilities) {
    const props = cap.payload_schema?.properties;
    const required = cap.payload_schema?.required;
    const propNames = props ? Object.keys(props).sort() : [];
    const ident = cppCapabilityIdent(cap.id);

    // Property names array (for additionalProperties check)
    if (propNames.length > 0) {
      const varName = `kProps_${ident}`;
      lines.push(`inline constexpr std::array<std::string_view, ${propNames.length}> ${varName} = {{`);
      for (const name of propNames) {
        lines.push(`    ${JSON.stringify(name)},`);
      }
      lines.push("}};");
      lines.push("");
    }

    // Required keys array
    if (required && required.length > 0) {
      const varName = `kRequired_${ident}`;
      lines.push(`inline constexpr std::array<std::string_view, ${required.length}> ${varName} = {{`);
      for (const name of required) {
        lines.push(`    ${JSON.stringify(name)},`);
      }
      lines.push("}};");
      lines.push("");
    }

    // Property types array
    if (props && propNames.length > 0) {
      const varName = `kPropTypes_${ident}`;
      lines.push(`inline constexpr std::array<PropertyMeta, ${propNames.length}> ${varName} = {{`);
      for (const name of propNames) {
        const propSchema = props[name];
        const propType = cppPropertyType(propSchema?.type);
        lines.push(`    {${JSON.stringify(name)}, PropertyType::${propType}},`);
      }
      lines.push("}};");
      lines.push("");
    }
  }

  lines.push(`inline constexpr size_t kCapabilityCount = ${capabilities.length};`);
  lines.push(`inline constexpr std::string_view kCapabilityRegistryDigest = "${digest}";`);
  lines.push("");
  lines.push(`inline constexpr std::array<CapabilityMeta, kCapabilityCount> kCapabilityRegistry = {{`);

  for (const cap of capabilities) {
    const hasSchema = cap.payload_schema !== null;
    const additionalProps = cap.payload_schema?.additionalProperties !== false;
    const props = cap.payload_schema?.properties;
    const required = cap.payload_schema?.required;
    const propNames = props ? Object.keys(props).sort() : [];
    const numProps = propNames.length;
    const numRequired = required?.length ?? 0;
    const ident = cppCapabilityIdent(cap.id);

    const propsPtr = numProps > 0 ? `kProps_${ident}.data()` : "nullptr";
    const requiredPtr = numRequired > 0 ? `kRequired_${ident}.data()` : "nullptr";
    const propTypesPtr = numProps > 0 ? `kPropTypes_${ident}.data()` : "nullptr";

    lines.push(`    {${JSON.stringify(cap.id)}, ${JSON.stringify(cap.rfc)}, ${JSON.stringify(cap.name)},`);
    lines.push(`     CapabilityStatus::${cppCapabilityStatus(cap.status)},`);
    lines.push(`     ${JSON.stringify(cap.doc)},`);
    lines.push(`     {.has_schema = ${hasSchema},`);
    lines.push(`      .additional_properties = ${additionalProps},`);
    lines.push(`      .allowed_keys = ${propsPtr},`);
    lines.push(`      .num_allowed_keys = ${numProps},`);
    lines.push(`      .required_keys = ${requiredPtr},`);
    lines.push(`      .num_required_keys = ${numRequired},`);
    lines.push(`      .property_types = ${propTypesPtr},`);
    lines.push(`      .num_property_types = ${numProps}}},`);
  }

  lines.push("}};");
  lines.push("");
  lines.push("} // namespace rankd");
  lines.push("");

  return lines.join("\n");
}

// ============================================================
// Main
// ============================================================

function main() {
  const checkMode = process.argv.includes("--check");
  const repoRoot = path.resolve(import.meta.dirname, "../..");

  // Parse registries
  const keys = parseKeys(path.join(repoRoot, "registry/keys.toml"));
  const params = parseParams(path.join(repoRoot, "registry/params.toml"));
  const features = parseFeatures(path.join(repoRoot, "registry/features.toml"));
  const validation = parseValidation(path.join(repoRoot, "registry/validation.toml"));
  const capabilities = parseCapabilities(path.join(repoRoot, "registry/capabilities.toml"));
  const tasks = parseTasks(path.join(repoRoot, "registry/tasks.toml"));

  // Build canonical JSON
  const keysCanonical = { schema_version: 1, entries: keys };
  const paramsCanonical = { schema_version: 1, entries: params };
  const featuresCanonical = { schema_version: 1, entries: features };
  const validationCanonical = { schema_version: 1, ...validation };
  const capabilitiesCanonical = { schema_version: 1, entries: capabilities };

  const keysDigest = computeDigest(keysCanonical);
  const paramsDigest = computeDigest(paramsCanonical);
  const featuresDigest = computeDigest(featuresCanonical);
  const validationDigest = computeDigest(validationCanonical);
  const capabilitiesDigest = computeDigest(capabilitiesCanonical);

  // Generate all outputs
  const outputs: Array<{ path: string; content: string }> = [
    // Artifacts JSON
    { path: "artifacts/keys.json", content: JSON.stringify(keysCanonical, null, 2) + "\n" },
    { path: "artifacts/params.json", content: JSON.stringify(paramsCanonical, null, 2) + "\n" },
    { path: "artifacts/features.json", content: JSON.stringify(featuresCanonical, null, 2) + "\n" },
    { path: "artifacts/capabilities.json", content: JSON.stringify(capabilitiesCanonical, null, 2) + "\n" },
    // Artifacts digests
    { path: "artifacts/keys.digest", content: keysDigest + "\n" },
    { path: "artifacts/params.digest", content: paramsDigest + "\n" },
    { path: "artifacts/features.digest", content: featuresDigest + "\n" },
    { path: "artifacts/capabilities.digest", content: capabilitiesDigest + "\n" },
    // TypeScript
    { path: "dsl/packages/generated/keys.ts", content: generateKeysTs(keys, keysDigest) },
    { path: "dsl/packages/generated/params.ts", content: generateParamsTs(params, paramsDigest) },
    { path: "dsl/packages/generated/features.ts", content: generateFeaturesTs(features, featuresDigest) },
    { path: "dsl/packages/generated/validation.ts", content: generateValidationTs(validation, validationDigest) },
    { path: "dsl/packages/generated/capabilities.ts", content: generateCapabilitiesTs(capabilities, capabilitiesDigest) },
    { path: "dsl/packages/generated/tasks.ts", content: generateTasksTs(tasks) },
    { path: "dsl/packages/generated/task-impl.ts", content: generateTaskImplTs(tasks) },
    { path: "dsl/packages/generated/index.ts", content: generateIndexTs() },
    // Plan globals declaration for editor support
    { path: "plan-globals.d.ts", content: generatePlanGlobalsDts() },
    // C++ headers (formatted with clang-format)
    // Note: We use *_registry.h names to avoid shadowing system headers (e.g. <features.h>)
    { path: "engine/include/key_registry.h", content: formatCpp(generateKeysH(keys, keysDigest)) },
    { path: "engine/include/param_registry.h", content: formatCpp(generateParamsH(params, paramsDigest)) },
    { path: "engine/include/feature_registry.h", content: formatCpp(generateFeaturesH(features, featuresDigest)) },
    { path: "engine/include/validation.h", content: formatCpp(generateValidationH(validation, validationDigest)) },
    { path: "engine/include/capability_registry_gen.h", content: formatCpp(generateCapabilitiesH(capabilities, capabilitiesDigest)) },
  ];

  if (checkMode) {
    let hasChanges = false;
    for (const { path: relPath, content } of outputs) {
      const fullPath = path.join(repoRoot, relPath);
      if (!existsSync(fullPath)) {
        console.error(`MISSING: ${relPath}`);
        hasChanges = true;
        continue;
      }
      const existing = readFileSync(fullPath, "utf-8");
      if (existing !== content) {
        console.error(`OUT OF SYNC: ${relPath}`);
        hasChanges = true;
      }
    }
    if (hasChanges) {
      console.error("\nRun 'pnpm -C dsl gen' to regenerate.");
      process.exit(1);
    }
    console.log("All generated files are in sync.");
  } else {
    for (const { path: relPath, content } of outputs) {
      const fullPath = path.join(repoRoot, relPath);
      writeFileSync(fullPath, content);
      console.log(`Generated: ${relPath}`);
    }
    console.log("\nCodegen complete.");
  }
}

main();
