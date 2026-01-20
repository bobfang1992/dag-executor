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
    trace?: string | null;
    extensions?: Record<string, unknown>;
}
export interface FilterOpts {
    pred: PredNode;
    trace?: string | null;
    extensions?: Record<string, unknown>;
}
export interface SortOpts {
    by: number;
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
export declare const TASK_MANIFEST_DIGEST = "491ba7faf5e41c95710d2f32ca1ad489ac2e4c06b5ab447c8e22e20972be77fe";
export declare const TASK_COUNT = 7;
//# sourceMappingURL=tasks.d.ts.map