// TOML parsing functions for codegen

import { readFileSync } from "fs";
import { parse as parseToml } from "@iarna/toml";
import type {
  KeyEntry,
  ParamEntry,
  FeatureEntry,
  CapabilityEntry,
  ValidationRules,
  TaskRegistry,
  TaskEntry,
  TaskParamEntry,
  JsonSchema,
  Status,
  EndpointKind,
  EndpointEntry,
  EndpointPolicy,
  StaticResolver,
} from "./types.js";
import {
  isKeyType,
  isParamType,
  isFeatureType,
  isStatus,
  isCapabilityStatus,
  isTaskParamType,
  isEndpointKind,
  isResolverType,
  assertString,
  assertNumber,
  assertInteger,
  assertBoolean,
} from "./types.js";

// =====================================================
// Parsing Functions
// =====================================================

export function parseKeys(tomlPath: string): KeyEntry[] {
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

export function parseParams(tomlPath: string): ParamEntry[] {
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

export function parseFeatures(tomlPath: string): FeatureEntry[] {
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

export function parseValidation(tomlPath: string): ValidationRules {
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

export function parseTasks(tomlPath: string): TaskRegistry {
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

        let endpoint_kind: EndpointKind | undefined = undefined;
        if (typeVal === "endpoint_ref") {
          if (p["endpoint_kind"] !== undefined) {
            if (!isEndpointKind(p["endpoint_kind"])) {
              throw new Error(
                `task ${op}: endpoint_kind must be one of ${["redis", "http"].join(", ")}`
              );
            }
            endpoint_kind = p["endpoint_kind"];
          }
        } else if (p["endpoint_kind"] !== undefined) {
          throw new Error(
            `task ${op}: endpoint_kind is only valid for endpoint_ref params`
          );
        }

        params.push({ name, type: typeVal, required, nullable, endpoint_kind });
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

export function parseCapabilities(tomlPath: string): CapabilityEntry[] {
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

// =====================================================
// Endpoint Parsing
// =====================================================

function parseEndpointFile(tomlPath: string, env: string): EndpointEntry[] {
  const content = readFileSync(tomlPath, "utf-8");
  const parsed: unknown = parseToml(content);

  if (typeof parsed !== "object" || parsed === null) {
    throw new Error(`Invalid TOML structure in ${tomlPath}`);
  }

  const obj = parsed as Record<string, unknown>;

  const version = obj["version"];
  if (version !== 1) {
    throw new Error(`Unsupported endpoints.${env}.toml version: ${version}`);
  }

  const rawEndpoints = obj["endpoint"];
  if (!Array.isArray(rawEndpoints)) {
    throw new Error(`Expected [[endpoint]] array in endpoints.${env}.toml`);
  }

  const entries: EndpointEntry[] = [];
  const seenIds = new Set<string>();
  const seenNames = new Set<string>();

  for (const raw of rawEndpoints) {
    if (typeof raw !== "object" || raw === null) {
      throw new Error(`Invalid endpoint entry in ${env}`);
    }
    const r = raw as Record<string, unknown>;

    const endpoint_id = assertString(r["endpoint_id"], "endpoint_id");
    const name = assertString(r["name"], "name");
    const kindVal = r["kind"];

    // Validate endpoint_id format
    if (!endpoint_id.startsWith("ep_")) {
      throw new Error(`endpoint_id must start with "ep_": ${endpoint_id}`);
    }
    if (endpoint_id.length > 64) {
      throw new Error(`endpoint_id too long (max 64): ${endpoint_id}`);
    }

    if (!isEndpointKind(kindVal)) {
      throw new Error(`Invalid endpoint kind: ${kindVal} (allowed: redis, http)`);
    }

    // Parse resolver
    const rawResolver = r["resolver"];
    if (typeof rawResolver !== "object" || rawResolver === null) {
      throw new Error(`endpoint ${endpoint_id}: missing resolver`);
    }
    const resolverObj = rawResolver as Record<string, unknown>;
    const resolverType = resolverObj["type"];
    if (!isResolverType(resolverType)) {
      throw new Error(`endpoint ${endpoint_id}: invalid resolver type: ${resolverType}`);
    }
    if (resolverType !== "static") {
      throw new Error(`endpoint ${endpoint_id}: only "static" resolver supported in Step 14.2, got: ${resolverType}`);
    }

    const host = assertString(resolverObj["host"], "resolver.host");
    const port = assertInteger(resolverObj["port"], "resolver.port");
    if (port < 1 || port > 65535) {
      throw new Error(`endpoint ${endpoint_id}: invalid port ${port} (must be 1-65535)`);
    }

    const resolver: StaticResolver = { type: "static", host, port };

    // Parse policy (optional fields)
    const policy: EndpointPolicy = {};
    const rawPolicy = r["policy"];
    if (rawPolicy !== undefined) {
      if (typeof rawPolicy !== "object" || rawPolicy === null) {
        throw new Error(`endpoint ${endpoint_id}: policy must be an object`);
      }
      const policyObj = rawPolicy as Record<string, unknown>;
      if (policyObj["max_inflight"] !== undefined) {
        policy.max_inflight = assertInteger(policyObj["max_inflight"], "policy.max_inflight");
      }
      if (policyObj["connect_timeout_ms"] !== undefined) {
        policy.connect_timeout_ms = assertInteger(policyObj["connect_timeout_ms"], "policy.connect_timeout_ms");
      }
      if (policyObj["request_timeout_ms"] !== undefined) {
        policy.request_timeout_ms = assertInteger(policyObj["request_timeout_ms"], "policy.request_timeout_ms");
      }
    }

    // Uniqueness checks
    if (seenIds.has(endpoint_id)) {
      throw new Error(`Duplicate endpoint_id in ${env}: ${endpoint_id}`);
    }
    if (seenNames.has(name)) {
      throw new Error(`Duplicate endpoint name in ${env}: ${name}`);
    }
    seenIds.add(endpoint_id);
    seenNames.add(name);

    entries.push({ endpoint_id, name, kind: kindVal, resolver, policy });
  }

  // Sort by endpoint_id for deterministic output
  entries.sort((a, b) => a.endpoint_id.localeCompare(b.endpoint_id));
  return entries;
}

export interface EndpointRegistries {
  dev: EndpointEntry[];
  test: EndpointEntry[];
  prod: EndpointEntry[];
}

export function parseEndpoints(registryDir: string): EndpointRegistries {
  const devPath = `${registryDir}/endpoints.dev.toml`;
  const testPath = `${registryDir}/endpoints.test.toml`;
  const prodPath = `${registryDir}/endpoints.prod.toml`;

  const dev = parseEndpointFile(devPath, "dev");
  const test = parseEndpointFile(testPath, "test");
  const prod = parseEndpointFile(prodPath, "prod");

  // Validate cross-env consistency: same endpoint_ids with same name/kind
  const devMap = new Map(dev.map(e => [e.endpoint_id, e]));
  const testMap = new Map(test.map(e => [e.endpoint_id, e]));
  const prodMap = new Map(prod.map(e => [e.endpoint_id, e]));

  // Check test has same IDs as dev
  for (const [id, devEntry] of devMap) {
    const testEntry = testMap.get(id);
    if (!testEntry) {
      throw new Error(`endpoint ${id} exists in dev but not in test`);
    }
    if (testEntry.name !== devEntry.name) {
      throw new Error(`endpoint ${id}: name mismatch (dev="${devEntry.name}", test="${testEntry.name}")`);
    }
    if (testEntry.kind !== devEntry.kind) {
      throw new Error(`endpoint ${id}: kind mismatch (dev="${devEntry.kind}", test="${testEntry.kind}")`);
    }
  }
  for (const id of testMap.keys()) {
    if (!devMap.has(id)) {
      throw new Error(`endpoint ${id} exists in test but not in dev`);
    }
  }

  // Check prod has same IDs as dev
  for (const [id, devEntry] of devMap) {
    const prodEntry = prodMap.get(id);
    if (!prodEntry) {
      throw new Error(`endpoint ${id} exists in dev but not in prod`);
    }
    if (prodEntry.name !== devEntry.name) {
      throw new Error(`endpoint ${id}: name mismatch (dev="${devEntry.name}", prod="${prodEntry.name}")`);
    }
    if (prodEntry.kind !== devEntry.kind) {
      throw new Error(`endpoint ${id}: kind mismatch (dev="${devEntry.kind}", prod="${prodEntry.kind}")`);
    }
  }
  for (const id of prodMap.keys()) {
    if (!devMap.has(id)) {
      throw new Error(`endpoint ${id} exists in prod but not in dev`);
    }
  }

  return { dev, test, prod };
}
