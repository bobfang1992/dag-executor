#include "writes_effect.h"
#include <algorithm>
#include <nlohmann/json.hpp>
#include <set>

namespace rankd {

// Helper for std::visit with multiple lambdas
template <class... Ts> struct overloaded : Ts... {
  using Ts::operator()...;
};
template <class... Ts> overloaded(Ts...) -> overloaded<Ts...>;

// Helper to merge and sort key vectors, removing duplicates
static std::vector<uint32_t> merge_keys(const std::vector<uint32_t> &a,
                                        const std::vector<uint32_t> &b) {
  std::set<uint32_t> merged(a.begin(), a.end());
  merged.insert(b.begin(), b.end());
  return std::vector<uint32_t>(merged.begin(), merged.end());
}

// Combine two WritesEffect results
static WritesEffect combine_effects(const WritesEffect &a,
                                    const WritesEffect &b) {
  // any Unknown => Unknown
  if (a.kind == EffectKind::Unknown || b.kind == EffectKind::Unknown) {
    return {EffectKind::Unknown, {}};
  }

  // all Exact => Exact, else May
  auto merged = merge_keys(a.keys, b.keys);

  if (a.kind == EffectKind::Exact && b.kind == EffectKind::Exact) {
    return {EffectKind::Exact, std::move(merged)};
  }

  return {EffectKind::May, std::move(merged)};
}

WritesEffect eval_writes(const WritesEffectExpr &expr,
                         const EffectGamma &gamma) {
  return std::visit(
      overloaded{
          [](const EffectKeys &e) -> WritesEffect {
            // Keys{} => always Exact with sorted, deduped keys (set semantics)
            std::set<uint32_t> deduped(e.key_ids.begin(), e.key_ids.end());
            return {EffectKind::Exact,
                    std::vector<uint32_t>(deduped.begin(), deduped.end())};
          },

          [&gamma](const EffectFromParam &e) -> WritesEffect {
            auto it = gamma.find(e.param);
            if (it != gamma.end() &&
                std::holds_alternative<uint32_t>(it->second)) {
              // Param is known at compile/link time as a key_id
              return {EffectKind::Exact, {std::get<uint32_t>(it->second)}};
            }
            // Param not known => Unknown
            return {EffectKind::Unknown, {}};
          },

          [&gamma](const EffectSwitchEnum &e) -> WritesEffect {
            auto it = gamma.find(e.param);
            if (it != gamma.end() &&
                std::holds_alternative<std::string>(it->second)) {
              // Param is known as a string enum value
              const auto &value = std::get<std::string>(it->second);
              auto case_it = e.cases.find(value);
              if (case_it != e.cases.end() && case_it->second) {
                return eval_writes(*case_it->second, gamma);
              }
              // Known value but no matching case => Unknown
              return {EffectKind::Unknown, {}};
            }

            // Param not constant => compute May(union of all cases)
            // If all cases are Exact, result is May(union), else Unknown
            if (e.cases.empty()) {
              return {EffectKind::Exact, {}};
            }

            bool all_bounded = true;
            std::set<uint32_t> all_keys;

            for (const auto &[case_name, case_expr] : e.cases) {
              if (!case_expr) {
                all_bounded = false;
                break;
              }
              WritesEffect case_result = eval_writes(*case_expr, gamma);
              if (case_result.kind == EffectKind::Unknown) {
                all_bounded = false;
                break;
              }
              all_keys.insert(case_result.keys.begin(), case_result.keys.end());
            }

            if (all_bounded) {
              return {EffectKind::May,
                      std::vector<uint32_t>(all_keys.begin(), all_keys.end())};
            }
            return {EffectKind::Unknown, {}};
          },

          [&gamma](const EffectUnion &e) -> WritesEffect {
            if (e.items.empty()) {
              return {EffectKind::Exact, {}};
            }

            WritesEffect result = {EffectKind::Exact, {}};
            for (const auto &item : e.items) {
              if (!item) {
                return {EffectKind::Unknown, {}};
              }
              WritesEffect item_result = eval_writes(*item, gamma);
              result = combine_effects(result, item_result);
              if (result.kind == EffectKind::Unknown) {
                break; // Short-circuit on Unknown
              }
            }
            return result;
          },
      },
      expr);
}

// Serialize writes_effect to JSON for manifest digest
std::string serialize_writes_effect(const WritesEffectExpr &expr) {
  nlohmann::ordered_json j;

  std::visit(
      overloaded{
          [&j](const EffectKeys &e) {
            j["kind"] = "Keys";
            // Sort and dedupe for canonical output (set semantics)
            std::set<uint32_t> deduped(e.key_ids.begin(), e.key_ids.end());
            j["key_ids"] =
                std::vector<uint32_t>(deduped.begin(), deduped.end());
          },

          [&j](const EffectFromParam &e) {
            j["kind"] = "FromParam";
            j["param"] = e.param;
          },

          [&j](const EffectSwitchEnum &e) {
            j["kind"] = "SwitchEnum";
            j["param"] = e.param;
            nlohmann::ordered_json cases_json;
            // Sort case keys for deterministic output
            std::vector<std::string> case_keys;
            for (const auto &[k, v] : e.cases) {
              case_keys.push_back(k);
            }
            std::sort(case_keys.begin(), case_keys.end());
            for (const auto &k : case_keys) {
              auto it = e.cases.find(k);
              if (it->second) {
                // Use ordered_json::parse to preserve key ordering
                cases_json[k] = nlohmann::ordered_json::parse(
                    serialize_writes_effect(*it->second));
              }
            }
            j["cases"] = cases_json;
          },

          [&j](const EffectUnion &e) {
            j["kind"] = "Union";
            nlohmann::ordered_json items_json = nlohmann::json::array();
            for (const auto &item : e.items) {
              if (item) {
                // Use ordered_json::parse to preserve key ordering
                items_json.push_back(nlohmann::ordered_json::parse(
                    serialize_writes_effect(*item)));
              }
            }
            j["items"] = items_json;
          },
      },
      expr);

  return j.dump();
}

} // namespace rankd
