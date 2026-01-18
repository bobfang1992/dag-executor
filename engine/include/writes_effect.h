#pragma once

#include <map>
#include <memory>
#include <string>
#include <variant>
#include <vector>

namespace rankd {

// Forward declare for recursive types
struct EffectKeys;
struct EffectFromParam;
struct EffectSwitchEnum;
struct EffectUnion;

// WritesEffectExpr is a variant of the four effect types
using WritesEffectExpr =
    std::variant<EffectKeys, EffectFromParam, EffectSwitchEnum, EffectUnion>;

// Keys{key_ids} -> always Exact({keys})
struct EffectKeys {
  std::vector<uint32_t> key_ids;
};

// FromParam("out") -> Exact if param constant, else Unknown
struct EffectFromParam {
  std::string param;
};

// SwitchEnum(param, cases) -> Exact if param constant, May if bounded, else
// Unknown
struct EffectSwitchEnum {
  std::string param;
  std::map<std::string, std::shared_ptr<WritesEffectExpr>> cases;
};

// Union([e1, e2, ...]) -> combines effects
struct EffectUnion {
  std::vector<std::shared_ptr<WritesEffectExpr>> items;
};

// Evaluation result kind
enum class EffectKind { Exact, May, Unknown };

// Evaluation result
struct WritesEffect {
  EffectKind kind;
  std::vector<uint32_t> keys; // empty if Unknown

  bool operator==(const WritesEffect &other) const {
    return kind == other.kind && keys == other.keys;
  }
};

// Gamma: compile/link-time env mapping param -> concrete value
// Value can be uint32_t (for key_id) or string (for enum case)
using EffectGamma = std::map<std::string, std::variant<uint32_t, std::string>>;

// Evaluate a writes_effect expression with given gamma context
WritesEffect eval_writes(const WritesEffectExpr &expr, const EffectGamma &gamma);

// Serialize writes_effect to JSON for manifest digest
std::string serialize_writes_effect(const WritesEffectExpr &expr);

// Helper to create EffectKeys
inline std::shared_ptr<WritesEffectExpr> makeEffectKeys(std::vector<uint32_t> key_ids) {
  return std::make_shared<WritesEffectExpr>(EffectKeys{std::move(key_ids)});
}

// Helper to create EffectFromParam
inline std::shared_ptr<WritesEffectExpr> makeEffectFromParam(std::string param) {
  return std::make_shared<WritesEffectExpr>(EffectFromParam{std::move(param)});
}

// Helper to create EffectSwitchEnum
inline std::shared_ptr<WritesEffectExpr> makeEffectSwitchEnum(
    std::string param,
    std::map<std::string, std::shared_ptr<WritesEffectExpr>> cases) {
  return std::make_shared<WritesEffectExpr>(
      EffectSwitchEnum{std::move(param), std::move(cases)});
}

// Helper to create EffectUnion
inline std::shared_ptr<WritesEffectExpr> makeEffectUnion(
    std::vector<std::shared_ptr<WritesEffectExpr>> items) {
  return std::make_shared<WritesEffectExpr>(EffectUnion{std::move(items)});
}

} // namespace rankd
