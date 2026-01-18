/**
 * @ranking-dsl/runtime - Plan authoring runtime.
 */

export { E, coalesce } from "./expr.js";
export type { ExprNode, StaticExprToken } from "./expr.js";

export { Pred } from "./pred.js";
export type { PredNode, RegexPattern } from "./pred.js";

export { definePlan, PlanCtx, CandidateSet } from "./plan.js";
export type { PlanDef, PlanArtifact } from "./plan.js";

// Re-export guards for validation
export {
  assertNotUndefined,
  checkNoUndefined,
  normalizeCapabilitiesRequired,
  isSortedUnique,
  validateCapabilitiesAndExtensions,
  validateNodeExtensions,
} from "./guards.js";

// Shared artifact validation (single source of truth for both compilers)
export { validateArtifact } from "./artifact-validation.js";

// RFC0005: writes_effect expression language
export {
  evalWrites,
  serializeWritesEffect,
  effectKeys,
  effectFromParam,
  effectSwitchEnum,
  effectUnion,
} from "./writes-effect.js";
export type {
  WritesEffectExpr,
  EffectKeys,
  EffectFromParam,
  EffectSwitchEnum,
  EffectUnion,
  WritesEffect,
  ExactEffect,
  MayEffect,
  UnknownEffect,
  EffectGamma,
} from "./writes-effect.js";

// Re-export tokens for convenience
export { Key, P } from "@ranking-dsl/generated";
export type { KeyToken, ParamToken } from "@ranking-dsl/generated";
