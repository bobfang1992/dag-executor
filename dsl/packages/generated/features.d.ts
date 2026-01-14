export type FeatureType = "int" | "float" | "string" | "bool";
export interface FeatureToken {
    readonly kind: "Feature";
    readonly id: number;
    readonly stage: string;
    readonly name: string;
}
export declare const Feat: {
    readonly esr: {
        readonly country: FeatureToken;
        readonly media_age_hours: FeatureToken;
    };
};
export declare const FEATURE_REGISTRY_DIGEST = "fd17942286e9ac42d9045869832c82cec7bb1b830ec488d65ae3ae924aba94ba";
export declare const FEATURE_COUNT = 2;
//# sourceMappingURL=features.d.ts.map