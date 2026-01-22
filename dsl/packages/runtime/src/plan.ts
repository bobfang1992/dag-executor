/**
 * Plan authoring API: CandidateSet, PlanCtx, definePlan.
 *
 * Types (ExprNode, PredNode, ExprInput, etc.) are imported from @ranking-dsl/generated
 * to ensure generated task option interfaces (VmOpts, FilterOpts) are compatible.
 *
 * Task implementations are generated from registry/tasks.toml via codegen.
 * This file only contains the PlanCtx, CandidateSet wrappers, and definePlan.
 */

import type { KeyToken, ExprNode, PredNode, ExprInput, RedisEndpointId } from "@ranking-dsl/generated";
import {
  mediaImpl,
  vmImpl,
  filterImpl,
  takeImpl,
  concatImpl,
  sortImpl,
  followImpl,
  recommendationImpl,
  sleepImpl,
} from "@ranking-dsl/generated";
import { assertNotUndefined, assertStringOrNull, assertEndpointId, checkNoUndefined } from "./guards.js";

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

  /**
   * viewer: get viewer's user data (returns single row with country).
   */
  viewer(opts: {
    endpoint: RedisEndpointId;
    trace?: string | null;
    extensions?: Record<string, unknown>;
  }): CandidateSet {
    // Source task - no inputs
    assertNotUndefined(opts, "viewer(opts)");
    assertNotUndefined(opts.endpoint, "viewer({ endpoint })");
    assertEndpointId(opts.endpoint, "viewer({ endpoint })");
    const { extensions, ...rest } = opts;
    checkNoUndefined(rest as Record<string, unknown>, "viewer(opts)");

    // Validate trace (same as generated tasks)
    if (opts.trace !== undefined) {
      assertStringOrNull(opts.trace, "viewer({ trace })");
    }

    const nodeId = this.addNode(
      "viewer",
      [],
      { endpoint: opts.endpoint, trace: opts.trace ?? null },
      extensions
    );
    return new CandidateSet(this, nodeId);
  }

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
    expr: ExprInput;
    trace?: string | null;
    extensions?: Record<string, unknown>;
  }): CandidateSet {
    const newNodeId = vmImpl(this.ctx, this.nodeId, opts);
    return new CandidateSet(this.ctx, newNodeId);
  }

  /**
   * filter: apply predicate to update selection.
   */
  filter(opts: {
    pred: PredNode;
    trace?: string | null;
    extensions?: Record<string, unknown>;
  }): CandidateSet {
    const newNodeId = filterImpl(this.ctx, this.nodeId, opts);
    return new CandidateSet(this.ctx, newNodeId);
  }

  /**
   * sort: reorder rows by a key (permutation only, no materialization).
   */
  sort(opts: {
    by: KeyToken;
    order?: "asc" | "desc";
    trace?: string | null;
    extensions?: Record<string, unknown>;
  }): CandidateSet {
    const newNodeId = sortImpl(this.ctx, this.nodeId, opts);
    return new CandidateSet(this.ctx, newNodeId);
  }

  /**
   * media: expand each row to its media items.
   */
  media(opts: {
    endpoint: RedisEndpointId;
    fanout: number;
    trace?: string | null;
    extensions?: Record<string, unknown>;
  }): CandidateSet {
    const newNodeId = mediaImpl(this.ctx, this.nodeId, opts);
    return new CandidateSet(this.ctx, newNodeId);
  }

  /**
   * follow: expand each user row to their follows.
   * For each input user ID, fetches up to 'fanout' followees.
   */
  follow(opts: {
    endpoint: RedisEndpointId;
    fanout: number;
    trace?: string | null;
    extensions?: Record<string, unknown>;
  }): CandidateSet {
    const newNodeId = followImpl(this.ctx, this.nodeId, opts);
    return new CandidateSet(this.ctx, newNodeId);
  }

  /**
   * recommendation: expand each user row to their recommendations.
   * For each input user ID, fetches up to 'fanout' recommendations.
   */
  recommendation(opts: {
    endpoint: RedisEndpointId;
    fanout: number;
    trace?: string | null;
    extensions?: Record<string, unknown>;
  }): CandidateSet {
    const newNodeId = recommendationImpl(this.ctx, this.nodeId, opts);
    return new CandidateSet(this.ctx, newNodeId);
  }

  /**
   * take: limit to first N rows.
   */
  take(opts: {
    count: number;
    trace?: string | null;
    extensions?: Record<string, unknown>;
  }): CandidateSet {
    const newNodeId = takeImpl(this.ctx, this.nodeId, opts);
    return new CandidateSet(this.ctx, newNodeId);
  }

  /**
   * sleep: delay execution for testing parallelism.
   */
  sleep(opts: {
    durationMs: number;
    trace?: string | null;
    extensions?: Record<string, unknown>;
  }): CandidateSet {
    const newNodeId = sleepImpl(this.ctx, this.nodeId, opts);
    return new CandidateSet(this.ctx, newNodeId);
  }

  /**
   * concat: concatenate two candidate sets.
   */
  concat(opts: {
    rhs: CandidateSet;
    trace?: string | null;
    extensions?: Record<string, unknown>;
  }): CandidateSet {
    assertNotUndefined(opts, "concat(opts)");
    assertNotUndefined(opts.rhs, "concat({ rhs })");
    if (opts.rhs.ctx !== this.ctx) {
      throw new Error(
        "concat: CandidateSets must belong to the same PlanCtx"
      );
    }
    const newNodeId = concatImpl(this.ctx, this.nodeId, opts);
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
