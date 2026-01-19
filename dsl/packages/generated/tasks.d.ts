import type { KeyToken } from "./keys.js";
/** Expression node (structural type matching @ranking-dsl/runtime ExprNode) */
export interface ExprNode {
    readonly op: string;
    [key: string]: unknown;
}
/** AST-extracted expression placeholder */
export interface StaticExprToken {
    readonly __expr_id: number;
}
/** Expression type for vm task */
export type VmExpr = ExprNode | StaticExprToken;
/** Predicate node (structural type matching @ranking-dsl/runtime PredNode) */
export interface PredNode {
    readonly op: string;
    [key: string]: unknown;
}
export interface ViewerFetchCachedRecommendationOpts {
    fanout: number;
    trace?: string;
    extensions?: Record<string, unknown>;
}
export interface ViewerFollowOpts {
    fanout: number;
    trace?: string;
    extensions?: Record<string, unknown>;
}
export interface ConcatOpts {
    trace?: string;
    extensions?: Record<string, unknown>;
}
export interface FilterOpts {
    pred: PredNode;
    trace?: string;
    extensions?: Record<string, unknown>;
}
export interface TakeOpts {
    count: number;
    trace?: string;
    extensions?: Record<string, unknown>;
}
export interface VmOpts {
    expr: VmExpr;
    outKey: KeyToken;
    trace?: string;
    extensions?: Record<string, unknown>;
}
export declare const TASK_MANIFEST_DIGEST = "687d722c1814f1481da590256799d799e3d0e20478fb4760f39ec20b91594e22";
export declare const TASK_COUNT = 6;
//# sourceMappingURL=tasks.d.ts.map