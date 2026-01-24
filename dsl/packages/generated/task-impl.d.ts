import type { ExprNode, ExprPlaceholder, ExprInput, PredNode, PredPlaceholder, PredInput, CandidateSetLike } from "./tasks.js";
import type { KeyToken } from "./keys.js";
import type { RedisEndpointId } from "./endpoints.js";
/** Interface for expression placeholder detection */
export declare function isExprPlaceholder(value: unknown): value is ExprPlaceholder;
/** Interface for predicate placeholder detection */
export declare function isPredPlaceholder(value: unknown): value is PredPlaceholder;
/** Interface that PlanCtx must implement for task impls to use */
export interface TaskContext {
    addNode(op: string, inputs: string[], params: Record<string, unknown>, extensions?: Record<string, unknown>): string;
    addExpr(expr: ExprNode): string;
    addPred(pred: PredNode): string;
}
/** Implementation for busy_cpu */
export declare function busyCpuImpl(ctx: TaskContext, inputNodeId: string, opts: {
    busyWaitMs: number;
    trace?: string | null;
    extensions?: Record<string, unknown>;
}): string;
/** Implementation for concat */
export declare function concatImpl(ctx: TaskContext, inputNodeId: string, opts: {
    rhs: CandidateSetLike;
    trace?: string | null;
    extensions?: Record<string, unknown>;
}): string;
/** Implementation for filter */
export declare function filterImpl(ctx: TaskContext, inputNodeId: string, opts: {
    pred: PredInput;
    trace?: string | null;
    extensions?: Record<string, unknown>;
}): string;
/** Implementation for fixed_source */
export declare function fixedSourceImpl(ctx: TaskContext, inputNodeId: string, opts: {
    rowCount?: number;
    trace?: string | null;
    extensions?: Record<string, unknown>;
}): string;
/** Implementation for follow */
export declare function followImpl(ctx: TaskContext, inputNodeId: string, opts: {
    endpoint: RedisEndpointId;
    fanout: number;
    trace?: string | null;
    extensions?: Record<string, unknown>;
}): string;
/** Implementation for media */
export declare function mediaImpl(ctx: TaskContext, inputNodeId: string, opts: {
    endpoint: RedisEndpointId;
    fanout: number;
    trace?: string | null;
    extensions?: Record<string, unknown>;
}): string;
/** Implementation for recommendation */
export declare function recommendationImpl(ctx: TaskContext, inputNodeId: string, opts: {
    endpoint: RedisEndpointId;
    fanout: number;
    trace?: string | null;
    extensions?: Record<string, unknown>;
}): string;
/** Implementation for sleep */
export declare function sleepImpl(ctx: TaskContext, inputNodeId: string, opts: {
    durationMs: number;
    failAfterSleep?: boolean;
    trace?: string | null;
    extensions?: Record<string, unknown>;
}): string;
/** Implementation for sort */
export declare function sortImpl(ctx: TaskContext, inputNodeId: string, opts: {
    by: KeyToken;
    order?: string;
    trace?: string | null;
    extensions?: Record<string, unknown>;
}): string;
/** Implementation for take */
export declare function takeImpl(ctx: TaskContext, inputNodeId: string, opts: {
    count: number;
    trace?: string | null;
    extensions?: Record<string, unknown>;
}): string;
/** Implementation for viewer */
export declare function viewerImpl(ctx: TaskContext, inputNodeId: string, opts: {
    endpoint: RedisEndpointId;
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
    readonly source: readonly [];
    readonly transform: readonly ["busyCpu", "concat", "filter", "fixedSource", "follow", "media", "recommendation", "sleep", "sort", "take", "viewer", "vm"];
};
//# sourceMappingURL=task-impl.d.ts.map