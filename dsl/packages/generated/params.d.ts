export type ParamType = "int" | "float" | "string" | "bool";
export interface ParamToken {
    readonly kind: "Param";
    readonly id: number;
    readonly name: string;
}
export declare const P: {
    readonly media_age_penalty_weight: ParamToken;
    readonly blocklist_regex: ParamToken;
    readonly esr_cutoff: ParamToken;
};
export declare const PARAM_REGISTRY_DIGEST = "02c816622d8b6f51eedb60d07d394984df1ac597ce81b76f1ad06ff2a3b0eb43";
export declare const PARAM_COUNT = 3;
//# sourceMappingURL=params.d.ts.map