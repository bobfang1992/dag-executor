export interface ValidationRules {
    schemaVersion: number;
    patterns: Record<string, RegExp>;
    limits: Record<string, number>;
    enums: Record<string, readonly string[]>;
}
export declare const Validation: ValidationRules;
export declare const VALIDATION_DIGEST = "54377dc5d599fdbbb14189ef616b28c0f77114997ecd07bde65a5899254d4a57";
export declare function isValidPlanName(value: string): boolean;
export declare function isValidNodeId(value: string): boolean;
//# sourceMappingURL=validation.d.ts.map