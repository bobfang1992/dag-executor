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
