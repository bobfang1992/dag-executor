#include "task_registry.h"
#include "param_table.h"
#include <set>
#include <stdexcept>
#include <unordered_map>

namespace rankd {

class ConcatTask {
public:
  static TaskSpec spec() {
    return TaskSpec{
        .op = "concat",
        .params_schema =
            {
                {.name = "rhs",
                 .type = TaskParamType::NodeRef,
                 .required = true},
                {.name = "trace",
                 .type = TaskParamType::String,
                 .required = false,
                 .nullable = true},
            },
        .reads = {},
        .writes = {},
        .default_budget = {.timeout_ms = 50},
        .output_pattern = OutputPattern::ConcatDense,
        // writes_effect omitted - no column writes
    };
  }

  static RowSet run(const std::vector<RowSet> &inputs,
                    [[maybe_unused]] const ValidatedParams &params,
                    const ExecCtx &ctx) {
    if (inputs.size() != 1) {
      throw std::runtime_error(
          "Error: op 'concat' expects exactly 1 input, got " +
          std::to_string(inputs.size()));
    }
    if (!ctx.resolved_node_refs || ctx.resolved_node_refs->find("rhs") == ctx.resolved_node_refs->end()) {
      throw std::runtime_error("Error: op 'concat' missing resolved 'rhs' NodeRef");
    }

    const auto &lhs = inputs[0];
    const auto &rhs = ctx.resolved_node_refs->at("rhs");

    // Materialize active indices
    auto lhsIdx = lhs.activeRows().toVector(lhs.rowCount());
    auto rhsIdx = rhs.activeRows().toVector(rhs.rowCount());

    size_t lhsN = lhsIdx.size();
    size_t rhsN = rhsIdx.size();
    size_t outN = lhsN + rhsN;

    // Create new batch
    auto outBatch = std::make_shared<ColumnBatch>(outN);

    // Copy ids: lhs active rows, then rhs active rows
    for (size_t i = 0; i < lhsN; ++i) {
      outBatch->setId(i, lhs.batch().getId(lhsIdx[i]));
    }
    for (size_t i = 0; i < rhsN; ++i) {
      outBatch->setId(lhsN + i, rhs.batch().getId(rhsIdx[i]));
    }

    // Union float columns
    std::set<uint32_t> float_keys;
    for (uint32_t k : lhs.batch().getFloatKeyIds())
      float_keys.insert(k);
    for (uint32_t k : rhs.batch().getFloatKeyIds())
      float_keys.insert(k);

    for (uint32_t key_id : float_keys) {
      auto col = std::make_shared<FloatColumn>(outN);
      const FloatColumn *lhsCol = lhs.batch().getFloatCol(key_id);
      const FloatColumn *rhsCol = rhs.batch().getFloatCol(key_id);

      // Copy lhs values
      for (size_t i = 0; i < lhsN; ++i) {
        if (lhsCol && lhsCol->valid[lhsIdx[i]]) {
          col->values[i] = lhsCol->values[lhsIdx[i]];
          col->valid[i] = 1;
        }
        // else valid stays 0
      }
      // Copy rhs values
      for (size_t i = 0; i < rhsN; ++i) {
        if (rhsCol && rhsCol->valid[rhsIdx[i]]) {
          col->values[lhsN + i] = rhsCol->values[rhsIdx[i]];
          col->valid[lhsN + i] = 1;
        }
      }

      *outBatch = outBatch->withFloatColumn(key_id, col);
    }

    // Union string columns with deterministic dict unification
    std::set<uint32_t> string_keys;
    for (uint32_t k : lhs.batch().getStringKeyIds())
      string_keys.insert(k);
    for (uint32_t k : rhs.batch().getStringKeyIds())
      string_keys.insert(k);

    for (uint32_t key_id : string_keys) {
      const StringDictColumn *lhsCol = lhs.batch().getStringCol(key_id);
      const StringDictColumn *rhsCol = rhs.batch().getStringCol(key_id);

      std::shared_ptr<const std::vector<std::string>> outDict;
      auto outCodes = std::make_shared<std::vector<int32_t>>(outN, 0);
      auto outValid = std::make_shared<std::vector<uint8_t>>(outN, 0);

      if (lhsCol && !rhsCol) {
        // Only lhs has the column
        outDict = lhsCol->dict;
        for (size_t i = 0; i < lhsN; ++i) {
          if ((*lhsCol->valid)[lhsIdx[i]]) {
            (*outCodes)[i] = (*lhsCol->codes)[lhsIdx[i]];
            (*outValid)[i] = 1;
          }
        }
        // rhs rows remain invalid
      } else if (!lhsCol && rhsCol) {
        // Only rhs has the column
        outDict = rhsCol->dict;
        for (size_t i = 0; i < rhsN; ++i) {
          if ((*rhsCol->valid)[rhsIdx[i]]) {
            (*outCodes)[lhsN + i] = (*rhsCol->codes)[rhsIdx[i]];
            (*outValid)[lhsN + i] = 1;
          }
        }
        // lhs rows remain invalid
      } else {
        // Both have the column - need dict unification
        // Fast path: same dict pointer or same content
        bool same_dict = (lhsCol->dict.get() == rhsCol->dict.get());
        if (!same_dict && lhsCol->dict->size() == rhsCol->dict->size()) {
          same_dict = (*lhsCol->dict == *rhsCol->dict);
        }

        if (same_dict) {
          // No remap needed
          outDict = lhsCol->dict;
          for (size_t i = 0; i < lhsN; ++i) {
            if ((*lhsCol->valid)[lhsIdx[i]]) {
              (*outCodes)[i] = (*lhsCol->codes)[lhsIdx[i]];
              (*outValid)[i] = 1;
            }
          }
          for (size_t i = 0; i < rhsN; ++i) {
            if ((*rhsCol->valid)[rhsIdx[i]]) {
              (*outCodes)[lhsN + i] = (*rhsCol->codes)[rhsIdx[i]];
              (*outValid)[lhsN + i] = 1;
            }
          }
        } else {
          // Merge dicts: lhs values in order + rhs values not already present
          auto mergedDict = std::make_shared<std::vector<std::string>>();
          std::unordered_map<std::string, int32_t> strToCode;

          // Add lhs dict entries
          for (size_t i = 0; i < lhsCol->dict->size(); ++i) {
            const std::string &s = (*lhsCol->dict)[i];
            strToCode[s] = static_cast<int32_t>(mergedDict->size());
            mergedDict->push_back(s);
          }

          // Build rhs remap: old code -> new code
          std::vector<int32_t> rhsRemap(rhsCol->dict->size());
          for (size_t i = 0; i < rhsCol->dict->size(); ++i) {
            const std::string &s = (*rhsCol->dict)[i];
            auto it = strToCode.find(s);
            if (it != strToCode.end()) {
              rhsRemap[i] = it->second;
            } else {
              rhsRemap[i] = static_cast<int32_t>(mergedDict->size());
              strToCode[s] = rhsRemap[i];
              mergedDict->push_back(s);
            }
          }

          outDict = mergedDict;

          // Copy lhs codes (no remap needed, same indices)
          for (size_t i = 0; i < lhsN; ++i) {
            if ((*lhsCol->valid)[lhsIdx[i]]) {
              (*outCodes)[i] = (*lhsCol->codes)[lhsIdx[i]];
              (*outValid)[i] = 1;
            }
          }
          // Copy rhs codes with remap
          for (size_t i = 0; i < rhsN; ++i) {
            if ((*rhsCol->valid)[rhsIdx[i]]) {
              int32_t oldCode = (*rhsCol->codes)[rhsIdx[i]];
              (*outCodes)[lhsN + i] = rhsRemap[static_cast<size_t>(oldCode)];
              (*outValid)[lhsN + i] = 1;
            }
          }
        }
      }

      auto outCol =
          std::make_shared<StringDictColumn>(outDict, outCodes, outValid);
      *outBatch = outBatch->withStringColumn(key_id, outCol);
    }

    return RowSet(std::make_shared<ColumnBatch>(*outBatch));
  }
};

// Auto-register this task
static TaskRegistrar<ConcatTask> registrar;

} // namespace rankd
