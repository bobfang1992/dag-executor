/**
 * Branded EndpointId type for type-safe endpoint references.
 * Use EP.redis.* or EP.http.* to get valid endpoint IDs.
 */
export type EndpointId = string & {
    readonly __brand: "EndpointId";
};
/**
 * Endpoint registry. Use EP.<kind>.<name> to get an EndpointId.
 * Example: EP.redis.default
 */
export declare const EP: {
    readonly http: {
        /** http_api (ep_0002) */
        readonly http_api: EndpointId;
    };
    readonly redis: {
        /** default (ep_0001) */
        readonly default: EndpointId;
    };
};
/** Type for http endpoint IDs */
export type HttpEndpointId = (typeof EP.http)[keyof typeof EP.http];
/** Type for redis endpoint IDs */
export type RedisEndpointId = (typeof EP.redis)[keyof typeof EP.redis];
/** Env-invariant registry digest (endpoint_id, name, kind only) */
export declare const ENDPOINT_REGISTRY_DIGEST = "aa596e6be6ac3ba1baef216420624ed5ea8a5a1fb6939071433e9bd7d426b0f1";
export declare const ENDPOINT_COUNT = 2;
//# sourceMappingURL=endpoints.d.ts.map