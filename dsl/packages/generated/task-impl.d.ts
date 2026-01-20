import type { ExprNode, ExprPlaceholder, ExprInput, PredNode } from "./tasks.js";
import type { KeyToken } from "./keys.js";
/** Interface for expression placeholder detection */
export declare function isExprPlaceholder(value: unknown): value is ExprPlaceholder;
/** Interface that PlanCtx must implement for task impls to use */
export interface TaskContext {
    addNode(op: string, inputs: string[], params: Record<string, unknown>, extensions?: Record<string, unknown>): string;
    addExpr(expr: ExprNode): string;
    addPred(pred: PredNode): string;
}
/** Implementation for viewer.fetch_cached_recommendation */
export declare function fetch_cached_recommendationImpl(ctx: TaskContext, opts: {
    fanout: number;
    trace?: string | null;
    extensions?: Record<string, unknown>;
}): string;
/** Implementation for viewer.follow */
export declare function followImpl(ctx: TaskContext, opts: {
    fanout: number;
    trace?: string | null;
    extensions?: Record<string, unknown>;
}): string;
/** Implementation for concat */
export declare function concatImpl(ctx: TaskContext, lhsNodeId: string, rhsNodeId: string, opts?: {
    trace?: string | null;
    extensions?: Record<string, unknown>;
}): string;
/** Implementation for filter */
export declare function filterImpl(ctx: TaskContext, inputNodeId: string, opts: {
    pred: PredNode;
    trace?: string | null;
    extensions?: Record<string, unknown>;
}): string;
/** Implementation for take */
export declare function takeImpl(ctx: TaskContext, inputNodeId: string, opts: {
    count: number;
    trace?: string | null;
    extensions?: Record<string, unknown>;
}): string;
/** Implementation for vm */
export declare function vmImpl(ctx: TaskContext, inputNodeId: string, opts: {
    expr: ExprInput;
    outKey: KeyToken;
    trace?: string | null;
    extensions?: Record<string, unknown>;
}): string;
export declare const GENERATED_TASKS: {
    readonly source: readonly ["fetch_cached_recommendation", "follow"];
    readonly transform: readonly ["concat", "filter", "take", "vm"];
};
//# sourceMappingURL=task-impl.d.ts.map