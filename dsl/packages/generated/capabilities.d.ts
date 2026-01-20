export interface CapabilityMeta {
    id: string;
    rfc: string;
    name: string;
    status: "implemented" | "draft" | "deprecated" | "blocked";
    doc: string;
    payload_schema: unknown;
}
export declare const CAP_RFC_0001_EXTENSIONS_CAPABILITIES_V1 = "cap.rfc.0001.extensions_capabilities.v1";
export declare const CAP_RFC_0005_KEY_EFFECTS_WRITES_EXACT_V1 = "cap.rfc.0005.key_effects_writes_exact.v1";
export declare const CapabilityRegistry: Record<string, CapabilityMeta>;
export declare const ALL_CAPABILITY_IDS: readonly ["cap.rfc.0001.extensions_capabilities.v1", "cap.rfc.0005.key_effects_writes_exact.v1"];
/** Set of capability IDs that are implemented and supported */
export declare const SUPPORTED_CAPABILITIES: Set<string>;
export declare const CAPABILITY_REGISTRY_DIGEST = "3537d0e4f5fe759dd852f53a81972534f50d14cb6e6a7772b5bf12cadb81d4c8";
export declare const CAPABILITY_COUNT = 2;
/**
 * Validate that a capability payload matches its schema.
 * Returns null if valid, error message if invalid.
 */
export declare function validatePayload(capId: string, payload: unknown): string | null;
//# sourceMappingURL=capabilities.d.ts.map