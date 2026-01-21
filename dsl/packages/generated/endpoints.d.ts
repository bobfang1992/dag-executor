/**
 * Branded EndpointId type for type-safe endpoint references.
 * Use EP.redis.* or EP.http.* to get valid endpoint IDs.
 */
export type EndpointId = string & {
    readonly __brand: "EndpointId";
};
type EndpointLiteral<T extends string> = EndpointId & T;
/**
 * Endpoint registry. Use EP.<kind>.<name> to get an EndpointId.
 * Example: EP.redis.default
 */
export declare const EP: {
    readonly http: {
        /** http_api (ep_0002) */
        readonly http_api: EndpointLiteral<"ep_0002">;
    };
    readonly redis: {
        /** redis_default (ep_0001) */
        readonly redis_default: EndpointLiteral<"ep_0001">;
    };
};
/** Type for http endpoint IDs */
export type HttpEndpointId = (typeof EP.http)[keyof typeof EP.http];
/** Type for redis endpoint IDs */
export type RedisEndpointId = (typeof EP.redis)[keyof typeof EP.redis];
/** Env-invariant registry digest (endpoint_id, name, kind only) */
export declare const ENDPOINT_REGISTRY_DIGEST = "551d9ea15487d6a5d38e8ee3e623b1ae858d87f977f680b605c8baf38c6a6024";
export declare const ENDPOINT_COUNT = 2;
export {};
//# sourceMappingURL=endpoints.d.ts.map