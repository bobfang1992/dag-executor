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
export declare const CAPABILITY_REGISTRY_DIGEST = "3537d0e4f5fe759dd852f53a81972534f50d14cb6e6a7772b5bf12cadb81d4c8";
export declare const CAPABILITY_COUNT = 2;
export declare function validatePayload(capId: string, payload: unknown): void;
//# sourceMappingURL=capabilities.d.ts.map