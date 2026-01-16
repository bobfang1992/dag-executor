export type CapabilityStatus = "implemented" | "draft" | "deprecated" | "blocked";
export interface JsonSchema {
    type?: string;
    properties?: Record<string, JsonSchema>;
    additionalProperties?: boolean;
    required?: string[];
}
export interface CapabilityMeta {
    id: string;
    rfc: string;
    name: string;
    status: CapabilityStatus;
    doc: string;
    payloadSchema: JsonSchema | null;
}
export declare const CAPABILITY_REGISTRY: Record<string, CapabilityMeta>;
export declare const SUPPORTED_CAPABILITIES: Set<string>;
export declare const CAPABILITY_REGISTRY_DIGEST = "470c3bc1e074d48d7a4e7f7807f44201bfddd4c97e09e575c796f197567dacfa";
export declare const CAPABILITY_COUNT = 1;
export declare function validatePayload(capId: string, payload: unknown): void;
//# sourceMappingURL=capabilities.d.ts.map