// Plan Index types (from index.json)

export interface PlanIndex {
  schema_version: number;
  plans: PlanIndexEntry[];
}

export interface PlanIndexEntry {
  name: string;
  path: string;
  digest: string;
  built_by?: {
    backend: string;
    tool: string;
    tool_version: string;
  };
}

// Plan JSON types (from compiled *.plan.json)

export interface PlanJson {
  schema_version: number;
  plan_name: string;
  nodes: PlanNode[];
  outputs: string[];
  expr_table?: Record<string, ExprNode>;
  pred_table?: Record<string, PredNode>;
  built_by?: {
    backend: string;
    tool: string;
    tool_version: string;
    bundle_digest?: string;
  };
}

export interface PlanNode {
  node_id: string;
  op: string;
  inputs: string[];
  params: Record<string, unknown>;
}

// Expression IR
export interface ExprNode {
  op: string;
  value?: number;
  key_id?: number;
  param_id?: number;
  a?: ExprNode;
  b?: ExprNode;
  x?: ExprNode;
}

// Predicate IR
export interface PredNode {
  op: string;
  value?: boolean;
  cmp?: string;
  key_id?: number;
  param_id?: number;
  a?: ExprNode | PredNode;
  b?: ExprNode | PredNode;
  x?: ExprNode | PredNode;
  lhs?: ExprNode;
  list?: (number | string)[];
  pattern?: { kind: string; value?: string; param_id?: number };
  flags?: string;
}

// Visualization types

export interface VisNode {
  id: string;
  op: string;
  label: string;
  trace: string | null;
  isSource: boolean;
  isOutput: boolean;
  x: number;
  y: number;
  width: number;
  height: number;
  params: Record<string, unknown>;
  // Fragment support (future)
  fragment?: string;
  fragmentVersion?: string;
  isFragment: boolean;
  children?: string[];
  expanded: boolean;
}

export interface VisEdge {
  id: string;
  from: string;
  to: string;
  hidden: boolean;
}

export interface VisGraph {
  nodes: Map<string, VisNode>;
  edges: VisEdge[];
}

// Registry types (from artifacts/*.json)

export interface KeyEntry {
  key_id: number;
  name: string;
  type: string;
  nullable: boolean;
  doc: string;
  status: 'active' | 'deprecated' | 'blocked';
  allow_read: boolean;
  allow_write: boolean;
  replaced_by: number | null;
  default: number | string | boolean | null;
}

export interface ParamEntry {
  param_id: number;
  name: string;
  type: string;
  nullable: boolean;
  doc: string;
  status: 'active' | 'deprecated' | 'blocked';
  allow_read: boolean;
  allow_write: boolean;
  replaced_by: number | null;
}

export interface FeatureEntry {
  feature_id: number;
  stage: string;
  name: string;
  type: string;
  nullable: boolean;
  status: 'active' | 'deprecated' | 'blocked';
  doc: string;
}

export interface CapabilityEntry {
  id: string;
  rfc: string;
  name: string;
  status: 'implemented' | 'draft' | 'deprecated' | 'blocked';
  doc: string;
  payload_schema: Record<string, unknown> | null;
}

export interface TaskParamEntry {
  name: string;
  type: string;
  required: boolean;
  nullable: boolean;
}

export interface TaskEntry {
  op: string;
  output_pattern: string;
  params: TaskParamEntry[];
  writes_effect?: unknown;
}

export interface EndpointEntry {
  endpoint_id: string;
  name: string;
  kind: string;
  resolver: {
    type: string;
    host: string;
    port: number;
  };
  policy: {
    max_inflight: number;
    connect_timeout_ms: number;
    request_timeout_ms: number;
  };
}

export type EndpointEnv = 'dev' | 'test' | 'prod';

export interface RegistryData {
  keys: KeyEntry[];
  params: ParamEntry[];
  features: FeatureEntry[];
  capabilities: CapabilityEntry[];
  tasks: TaskEntry[];
  endpoints: Record<EndpointEnv, EndpointEntry[]>;
}

export type RegistryTab = 'keys' | 'params' | 'features' | 'capabilities' | 'tasks' | 'endpoints';
