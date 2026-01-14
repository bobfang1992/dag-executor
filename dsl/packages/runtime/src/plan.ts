/**
 * Plan authoring API: CandidateSet, PlanCtx, definePlan.
 */

import type { KeyToken } from "@ranking-dsl/generated";
import { assertNotUndefined, checkNoUndefined } from "./guards.js";
import type { ExprNode } from "./expr.js";
import type { PredNode } from "./pred.js";

/**
 * Internal node representation (before JSON serialization).
 */
interface PlanNode {
  node_id: string;
  op: string;
  inputs: string[];
  params: Record<string, unknown>;
}

/**
 * PlanContext accumulates nodes and provides source constructors.
 */
export class PlanCtx {
  private nodeCounter = 0;
  private nodes: PlanNode[] = [];
  private exprTable: Map<string, ExprNode> = new Map();
  private predTable: Map<string, PredNode> = new Map();

  readonly viewer = {
    follow: (opts: { fanout: number; trace?: string }): CandidateSet => {
      assertNotUndefined(opts, "viewer.follow(opts)");
      assertNotUndefined(opts.fanout, "viewer.follow({ fanout })");
      checkNoUndefined(opts as Record<string, unknown>, "viewer.follow(opts)");

      const nodeId = this.allocateNodeId();
      this.nodes.push({
        node_id: nodeId,
        op: "viewer.follow",
        inputs: [],
        params: { fanout: opts.fanout, trace: opts.trace ?? null },
      });
      return new CandidateSet(this, nodeId);
    },

    fetch_cached_recommendation: (opts: { fanout: number; trace?: string }): CandidateSet => {
      assertNotUndefined(opts, "viewer.fetch_cached_recommendation(opts)");
      assertNotUndefined(opts.fanout, "viewer.fetch_cached_recommendation({ fanout })");
      checkNoUndefined(opts as Record<string, unknown>, "viewer.fetch_cached_recommendation(opts)");

      const nodeId = this.allocateNodeId();
      this.nodes.push({
        node_id: nodeId,
        op: "viewer.fetch_cached_recommendation",
        inputs: [],
        params: { fanout: opts.fanout, trace: opts.trace ?? null },
      });
      return new CandidateSet(this, nodeId);
    },
  };

  private allocateNodeId(): string {
    return `n${this.nodeCounter++}`;
  }

  addNode(op: string, inputs: string[], params: Record<string, unknown>): string {
    const nodeId = this.allocateNodeId();
    this.nodes.push({ node_id: nodeId, op, inputs, params });
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

  finalize(outputNodeId: string, planName: string): PlanArtifact {
    return {
      schema_version: 1,
      plan_name: planName,
      nodes: this.nodes,
      outputs: [outputNodeId],
      expr_table: this.exprTable.size > 0 ? Object.fromEntries(this.exprTable) : undefined,
      pred_table: this.predTable.size > 0 ? Object.fromEntries(this.predTable) : undefined,
    };
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
   */
  vm(opts: { outKey: KeyToken; expr: ExprNode; trace?: string }): CandidateSet {
    assertNotUndefined(opts, "vm(opts)");
    assertNotUndefined(opts.outKey, "vm({ outKey })");
    assertNotUndefined(opts.expr, "vm({ expr })");
    checkNoUndefined(opts as Record<string, unknown>, "vm(opts)");

    const exprId = this.ctx.addExpr(opts.expr);
    const newNodeId = this.ctx.addNode("vm", [this.nodeId], {
      out_key: opts.outKey.id,
      expr_id: exprId,
      trace: opts.trace ?? null,
    });
    return new CandidateSet(this.ctx, newNodeId);
  }

  /**
   * filter: apply predicate to update selection.
   */
  filter(opts: { pred: PredNode; trace?: string }): CandidateSet {
    assertNotUndefined(opts, "filter(opts)");
    assertNotUndefined(opts.pred, "filter({ pred })");
    checkNoUndefined(opts as Record<string, unknown>, "filter(opts)");

    const predId = this.ctx.addPred(opts.pred);
    const newNodeId = this.ctx.addNode("filter", [this.nodeId], {
      pred_id: predId,
      trace: opts.trace ?? null,
    });
    return new CandidateSet(this.ctx, newNodeId);
  }

  /**
   * take: limit to first N rows.
   */
  take(opts: { count: number; trace?: string }): CandidateSet {
    assertNotUndefined(opts, "take(opts)");
    assertNotUndefined(opts.count, "take({ count })");
    checkNoUndefined(opts as Record<string, unknown>, "take(opts)");

    const newNodeId = this.ctx.addNode("take", [this.nodeId], {
      count: opts.count,
      trace: opts.trace ?? null,
    });
    return new CandidateSet(this.ctx, newNodeId);
  }

  /**
   * concat: concatenate two candidate sets.
   */
  concat(rhs: CandidateSet, opts?: { trace?: string }): CandidateSet {
    assertNotUndefined(rhs, "concat(rhs)");
    if (rhs.ctx !== this.ctx) {
      throw new Error(
        "concat: CandidateSets must belong to the same PlanCtx"
      );
    }
    if (opts !== undefined) {
      checkNoUndefined(opts as Record<string, unknown>, "concat(opts)");
    }

    const newNodeId = this.ctx.addNode("concat", [this.nodeId, rhs.nodeId], {
      trace: opts?.trace ?? null,
    });
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
