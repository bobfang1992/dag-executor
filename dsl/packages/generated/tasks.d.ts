import type { KeyToken } from "./keys.js";
/** ExprNode - matches engine's ExprIR format (builder-style) */
export type ExprNode = {
    op: "const_number";
    value: number;
} | {
    op: "const_null";
} | {
    op: "key_ref";
    key_id: number;
} | {
    op: "param_ref";
    param_id: number;
} | {
    op: "add";
    a: ExprNode;
    b: ExprNode;
} | {
    op: "sub";
    a: ExprNode;
    b: ExprNode;
} | {
    op: "mul";
    a: ExprNode;
    b: ExprNode;
} | {
    op: "neg";
    x: ExprNode;
} | {
    op: "coalesce";
    a: ExprNode;
    b: ExprNode;
};
/** ExprPlaceholder - compile-time placeholder for natural expression syntax */
export interface ExprPlaceholder {
    __expr_id: number;
}
/** ExprInput - expression input type for tasks (builder or natural syntax) */
export type ExprInput = ExprNode | ExprPlaceholder;
/** Regex pattern - literal string or param reference */
export type RegexPattern = {
    kind: "literal";
    value: string;
} | {
    kind: "param";
    param_id: number;
};
/** PredNode - matches engine's PredIR format */
export type PredNode = {
    op: "const_bool";
    value: boolean;
} | {
    op: "and";
    a: PredNode;
    b: PredNode;
} | {
    op: "or";
    a: PredNode;
    b: PredNode;
} | {
    op: "not";
    x: PredNode;
} | {
    op: "cmp";
    cmp: "==" | "!=" | "<" | "<=" | ">" | ">=";
    a: ExprNode;
    b: ExprNode;
} | {
    op: "in";
    lhs: ExprNode;
    list: (number | string)[];
} | {
    op: "is_null";
    x: ExprNode;
} | {
    op: "not_null";
    x: ExprNode;
} | {
    op: "regex";
    key_id: number;
    pattern: RegexPattern;
    flags: string;
};
/** PredPlaceholder - compile-time placeholder for natural predicate syntax */
export interface PredPlaceholder {
    __pred_id: number;
}
/** PredInput - predicate input type for tasks (builder or natural syntax) */
export type PredInput = PredNode | PredPlaceholder;
/** Interface for CandidateSet used by NodeRef params (avoids circular dep with runtime) */
export interface CandidateSetLike {
    getNodeId(): string;
}
export interface ViewerFetchCachedRecommendationOpts {
    fanout: number;
    trace?: string | null;
    extensions?: Record<string, unknown>;
}
export interface ViewerFollowOpts {
    fanout: number;
    trace?: string | null;
    extensions?: Record<string, unknown>;
}
export interface ConcatOpts {
    rhs: CandidateSetLike;
    trace?: string | null;
    extensions?: Record<string, unknown>;
}
export interface FilterOpts {
    pred: PredInput;
    trace?: string | null;
    extensions?: Record<string, unknown>;
}
export interface SortOpts {
    by: KeyToken;
    order?: string;
    trace?: string | null;
    extensions?: Record<string, unknown>;
}
export interface TakeOpts {
    count: number;
    trace?: string | null;
    extensions?: Record<string, unknown>;
}
export interface VmOpts {
    expr: ExprInput;
    outKey: KeyToken;
    trace?: string | null;
    extensions?: Record<string, unknown>;
}
export declare const TASK_MANIFEST_DIGEST = "c02b01d9e7d4f7c0a2ef2734f12b16c76f83c6f8d0e12b6426cf816debe839c0";
export declare const TASK_COUNT = 7;
/** Extraction info for a task - which properties to extract as expr/pred */
export interface TaskExtractionInfo {
    /** Property name containing expression (for tasks with expr_id param) */
    exprProp?: string;
    /** Property name containing predicate (for tasks with pred_id param) */
    predProp?: string;
}
/** Map from method name to extraction info */
export declare const TASK_EXTRACTION_INFO: Record<string, TaskExtractionInfo>;
//# sourceMappingURL=tasks.d.ts.map