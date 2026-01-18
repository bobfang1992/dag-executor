/**
 * Plan authoring API: CandidateSet, PlanCtx, definePlan.
 */

import type { KeyToken } from "@ranking-dsl/generated";
import { assertNotUndefined, checkNoUndefined } from "./guards.js";
import type { ExprNode, StaticExprToken } from "./expr.js";
import type { PredNode } from "./pred.js";

/** Type for expression that can be passed to vm() */
type VmExpr = ExprNode | StaticExprToken;

/** Check if value is a StaticExprToken (AST-extracted placeholder) */
function isStaticExprToken(value: unknown): value is StaticExprToken {
  return (
    value !== null &&
    typeof value === "object" &&
    "__expr_id" in value &&
    typeof (value as StaticExprToken).__expr_id === "number"
  );
}

/**
 * Internal node representation (before JSON serialization).
 */
interface PlanNode {
  node_id: string;
  op: string;
  inputs: string[];
  params: Record<string, unknown>;
  extensions?: Record<string, unknown>;
}

/**
 * PlanContext accumulates nodes and provides source constructors.
 */
export class PlanCtx {
  private nodeCounter = 0;
  private nodes: PlanNode[] = [];
  private exprTable: Map<string, ExprNode> = new Map();
  private predTable: Map<string, PredNode> = new Map();
  private capabilitiesRequired: Set<string> = new Set();
  private planExtensions: Map<string, unknown> = new Map();

  readonly viewer = {
    follow: (opts: {
      fanout: number;
      trace?: string;
      extensions?: Record<string, unknown>;
    }): CandidateSet => {
      assertNotUndefined(opts, "viewer.follow(opts)");
      assertNotUndefined(opts.fanout, "viewer.follow({ fanout })");
      const { extensions, ...rest } = opts;
      checkNoUndefined(rest as Record<string, unknown>, "viewer.follow(opts)");

      const nodeId = this.allocateNodeId();
      const node: PlanNode = {
        node_id: nodeId,
        op: "viewer.follow",
        inputs: [],
        params: { fanout: opts.fanout, trace: opts.trace ?? null },
      };
      if (extensions !== undefined && Object.keys(extensions).length > 0) {
        checkNoUndefined(extensions, `node[${nodeId}].extensions`);
        node.extensions = extensions;
      }
      this.nodes.push(node);
      return new CandidateSet(this, nodeId);
    },

    fetch_cached_recommendation: (opts: {
      fanout: number;
      trace?: string;
      extensions?: Record<string, unknown>;
    }): CandidateSet => {
      assertNotUndefined(opts, "viewer.fetch_cached_recommendation(opts)");
      assertNotUndefined(opts.fanout, "viewer.fetch_cached_recommendation({ fanout })");
      const { extensions, ...rest } = opts;
      checkNoUndefined(
        rest as Record<string, unknown>,
        "viewer.fetch_cached_recommendation(opts)"
      );

      const nodeId = this.allocateNodeId();
      const node: PlanNode = {
        node_id: nodeId,
        op: "viewer.fetch_cached_recommendation",
        inputs: [],
        params: { fanout: opts.fanout, trace: opts.trace ?? null },
      };
      if (extensions !== undefined && Object.keys(extensions).length > 0) {
        checkNoUndefined(extensions, `node[${nodeId}].extensions`);
        node.extensions = extensions;
      }
      this.nodes.push(node);
      return new CandidateSet(this, nodeId);
    },
  };

  private allocateNodeId(): string {
    return `n${this.nodeCounter++}`;
  }

  addNode(
    op: string,
    inputs: string[],
    params: Record<string, unknown>,
    extensions?: Record<string, unknown>
  ): string {
    const nodeId = this.allocateNodeId();
    const node: PlanNode = { node_id: nodeId, op, inputs, params };
    if (extensions !== undefined && Object.keys(extensions).length > 0) {
      checkNoUndefined(extensions, `node[${nodeId}].extensions`);
      node.extensions = extensions;
    }
    this.nodes.push(node);
    return nodeId;
  }

  addExpr(expr: ExprNode): string {
    const exprId = `e${this.exprTable.size}`;
    this.exprTable.set(exprId, expr);
    return exprId;
  }

  addPred(pred: PredNode): string {
    const predId = `p${this.predTable.size}`;
    this.predTable.set(predId, pred);
    return predId;
  }

  /**
   * Declare a required capability for this plan.
   * If payload is provided, it is stored in plan-level extensions under the capability ID.
   */
  requireCapability(capId: string, payload?: unknown): void {
    assertNotUndefined(capId, "requireCapability(capId)");
    if (capId.length === 0) {
      throw new Error("Capability ID cannot be empty");
    }
    this.capabilitiesRequired.add(capId);
    if (payload !== undefined) {
      assertNotUndefined(payload, `requireCapability("${capId}", payload)`);
      if (typeof payload === "object" && payload !== null && !Array.isArray(payload)) {
        checkNoUndefined(payload as Record<string, unknown>, `extensions["${capId}"]`);
      }
      this.planExtensions.set(capId, payload);
    }
  }

  finalize(outputNodeId: string, planName: string): PlanArtifact {
    const artifact: PlanArtifact = {
      schema_version: 1,
      plan_name: planName,
      nodes: this.nodes,
      outputs: [outputNodeId],
    };

    // Only include expr_table and pred_table if they contain entries
    if (this.exprTable.size > 0) {
      artifact.expr_table = Object.fromEntries(this.exprTable);
    }
    if (this.predTable.size > 0) {
      artifact.pred_table = Object.fromEntries(this.predTable);
    }

    // Only include capabilities_required if any are declared (sorted + unique)
    if (this.capabilitiesRequired.size > 0) {
      artifact.capabilities_required = [...this.capabilitiesRequired].sort();
    }

    // Only include extensions if any are provided
    if (this.planExtensions.size > 0) {
      artifact.extensions = Object.fromEntries(this.planExtensions);
    }

    return artifact;
  }
}

/**
 * CandidateSet - immutable builder for plan nodes.
 */
export class CandidateSet {
  constructor(
    private readonly ctx: PlanCtx,
    private readonly nodeId: string
  ) {}

  /**
   * vm: evaluate expression and write to out_key.
   *
   * Natural expressions are AST-extracted at compile time:
   *   c.vm({ outKey: Key.final_score, expr: Key.id * coalesce(P.weight, 0.2) })
   */
  vm(opts: {
    outKey: KeyToken;
    expr: VmExpr;
    trace?: string;
    extensions?: Record<string, unknown>;
  }): CandidateSet {
    assertNotUndefined(opts, "vm(opts)");
    assertNotUndefined(opts.outKey, "vm({ outKey })");
    assertNotUndefined(opts.expr, "vm({ expr })");
    const { extensions, ...rest } = opts;
    checkNoUndefined(rest as Record<string, unknown>, "vm(opts)");

    // Handle StaticExprToken vs regular ExprNode
    let exprId: string;
    if (isStaticExprToken(opts.expr)) {
      // AST-extracted expression - use special prefix for later remapping
      exprId = `__static_e${opts.expr.__expr_id}`;
    } else {
      // Regular builder-style expression
      exprId = this.ctx.addExpr(opts.expr);
    }

    const newNodeId = this.ctx.addNode(
      "vm",
      [this.nodeId],
      {
        out_key: opts.outKey.id,
        expr_id: exprId,
        trace: opts.trace ?? null,
      },
      extensions
    );
    return new CandidateSet(this.ctx, newNodeId);
  }

  /**
   * filter: apply predicate to update selection.
   */
  filter(opts: {
    pred: PredNode;
    trace?: string;
    extensions?: Record<string, unknown>;
  }): CandidateSet {
    assertNotUndefined(opts, "filter(opts)");
    assertNotUndefined(opts.pred, "filter({ pred })");
    const { extensions, ...rest } = opts;
    checkNoUndefined(rest as Record<string, unknown>, "filter(opts)");

    const predId = this.ctx.addPred(opts.pred);
    const newNodeId = this.ctx.addNode(
      "filter",
      [this.nodeId],
      {
        pred_id: predId,
        trace: opts.trace ?? null,
      },
      extensions
    );
    return new CandidateSet(this.ctx, newNodeId);
  }

  /**
   * take: limit to first N rows.
   */
  take(opts: {
    count: number;
    trace?: string;
    extensions?: Record<string, unknown>;
  }): CandidateSet {
    assertNotUndefined(opts, "take(opts)");
    assertNotUndefined(opts.count, "take({ count })");
    const { extensions, ...rest } = opts;
    checkNoUndefined(rest as Record<string, unknown>, "take(opts)");

    const newNodeId = this.ctx.addNode(
      "take",
      [this.nodeId],
      {
        count: opts.count,
        trace: opts.trace ?? null,
      },
      extensions
    );
    return new CandidateSet(this.ctx, newNodeId);
  }

  /**
   * concat: concatenate two candidate sets.
   */
  concat(
    rhs: CandidateSet,
    opts?: { trace?: string; extensions?: Record<string, unknown> }
  ): CandidateSet {
    assertNotUndefined(rhs, "concat(rhs)");
    if (rhs.ctx !== this.ctx) {
      throw new Error(
        "concat: CandidateSets must belong to the same PlanCtx"
      );
    }
    let extensions: Record<string, unknown> | undefined;
    if (opts !== undefined) {
      const { extensions: ext, ...rest } = opts;
      extensions = ext;
      checkNoUndefined(rest as Record<string, unknown>, "concat(opts)");
    }

    const newNodeId = this.ctx.addNode(
      "concat",
      [this.nodeId, rhs.nodeId],
      {
        trace: opts?.trace ?? null,
      },
      extensions
    );
    return new CandidateSet(this.ctx, newNodeId);
  }

  getNodeId(): string {
    return this.nodeId;
  }
}

/**
 * Plan artifact - JSON output.
 */
export interface PlanArtifact {
  schema_version: number;
  plan_name: string;
  nodes: PlanNode[];
  outputs: string[];
  expr_table?: Record<string, ExprNode>;
  pred_table?: Record<string, PredNode>;
  capabilities_required?: string[];
  extensions?: Record<string, unknown>;
}

/**
 * Plan definition.
 */
export interface PlanDef {
  name: string;
  build: (ctx: PlanCtx) => CandidateSet;
}

/**
 * definePlan - main entry point for plan authoring.
 *
 * When running in QuickJS sandbox (via dslc), this function will:
 * 1. Build the plan
 * 2. Finalize to artifact
 * 3. Call globalThis.__emitPlan() to emit the artifact
 *
 * When running in Node (via compiler-node), it returns a PlanDef for direct use.
 */
export function definePlan(spec: {
  name: string;
  build: (ctx: PlanCtx) => CandidateSet;
}): PlanDef {
  assertNotUndefined(spec, "definePlan(spec)");
  assertNotUndefined(spec.name, "definePlan({ name })");
  assertNotUndefined(spec.build, "definePlan({ build })");
  checkNoUndefined(spec as Record<string, unknown>, "definePlan(spec)");

  const planDef: PlanDef = {
    name: spec.name,
    build: spec.build,
  };

  // QuickJS mode: immediately build and emit
  // Check for __emitPlan in globalThis (QuickJS sandbox)
  if (typeof globalThis !== "undefined" && "__emitPlan" in globalThis) {
    const emitFn = (globalThis as Record<string, unknown>).__emitPlan;
    if (typeof emitFn === "function") {
      const ctx = new PlanCtx();
      const result = spec.build(ctx);
      const artifact = ctx.finalize(result.getNodeId(), spec.name);
      emitFn(artifact);
    }
  }

  // Node mode: return PlanDef for direct use
  return planDef;
}
