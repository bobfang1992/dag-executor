export type KeyType = "int" | "float" | "string" | "bool" | "feature_bundle";
export interface KeyToken {
    readonly kind: "Key";
    readonly id: number;
    readonly name: string;
}
export declare const Key: {
    readonly id: KeyToken;
    readonly model_score_1: KeyToken;
    readonly model_score_2: KeyToken;
    readonly final_score: KeyToken;
    readonly country: KeyToken;
    readonly title: KeyToken;
    readonly features_esr: KeyToken;
    readonly features_lsr: KeyToken;
};
export declare const KEY_REGISTRY_DIGEST = "80e926e8520e814505a2150c66e9069615c0b500e8ea17b1d4a63b83df650852";
export declare const KEY_COUNT = 8;
//# sourceMappingURL=keys.d.ts.map