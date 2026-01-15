export declare const PLAN_NAME_PATTERN: RegExp;
export declare const NODE_ID_PATTERN: RegExp;
export declare function isValidPlanName(value: string): boolean;
export declare function isValidNodeId(value: string): boolean;
export declare const MAX_FANOUT = 10000000;
export declare const PLAN_SCHEMA_VERSION = 1;
export declare const REGEX_FLAGS: readonly ["", "i"];
export type RegexFlags = typeof REGEX_FLAGS[number];
export declare const CMP_OPERATORS: readonly ["==", "!=", "<", "<=", ">", ">="];
export type CmpOperators = typeof CMP_OPERATORS[number];
export declare const VALIDATION_DIGEST = "54377dc5d599fdbbb14189ef616b28c0f77114997ecd07bde65a5899254d4a57";
//# sourceMappingURL=validation.d.ts.map