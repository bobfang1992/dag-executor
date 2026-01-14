#include "task_registry.h"
#include "expr_eval.h"
#include "param_table.h"
#include "pred_eval.h"
#include "sha256.h"
#include <algorithm>
#include <cmath>
#include <limits>
#include <set>
#include <sstream>
#include <stdexcept>
#include <unordered_map>
#include <unordered_set>

namespace rankd {

TaskRegistry &TaskRegistry::instance() {
  static TaskRegistry reg;
  return reg;
}

TaskRegistry::TaskRegistry() {
  // Register viewer.follow
  TaskSpec viewer_follow_spec{
      .op = "viewer.follow",
      .params_schema =
          {
              {.name = "fanout", .type = TaskParamType::Int, .required = true},
              {.name = "trace",
               .type = TaskParamType::String,
               .required = false,
               .nullable = true},
          },
      .reads = {},
      .writes = {},
      .default_budget = {.timeout_ms = 100},
      .output_pattern = OutputPattern::SourceFanoutDense,
  };

  register_task(
      std::move(viewer_follow_spec),
      [](const std::vector<RowSet> &inputs, const ValidatedParams &params,
         [[maybe_unused]] const ExecCtx &ctx) -> RowSet {
        if (!inputs.empty()) {
          throw std::runtime_error("viewer.follow: expected 0 inputs");
        }
        int64_t fanout = params.get_int("fanout");
        if (fanout <= 0) {
          throw std::runtime_error("viewer.follow: 'fanout' must be > 0");
        }
        constexpr int64_t kMaxFanout = 10'000'000; // 10M limit
        if (fanout > kMaxFanout) {
          throw std::runtime_error(
              "viewer.follow: 'fanout' exceeds maximum limit (10000000)");
        }

        size_t n = static_cast<size_t>(fanout);

        // Create ColumnBatch with ids 1..fanout
        auto batch = std::make_shared<ColumnBatch>(n);
        for (size_t i = 0; i < n; ++i) {
          batch->setId(i, static_cast<int64_t>(i + 1)); // ids are 1-indexed
        }

        // Add country column: dict ["US","CA"], pattern US,CA,US,CA...
        auto country_dict = std::make_shared<std::vector<std::string>>(
            std::vector<std::string>{"US", "CA"});
        auto country_codes = std::make_shared<std::vector<int32_t>>(n);
        auto country_valid = std::make_shared<std::vector<uint8_t>>(n, 1);
        for (size_t i = 0; i < n; ++i) {
          (*country_codes)[i] = static_cast<int32_t>(i % 2); // 0=US, 1=CA
        }
        auto country_col = std::make_shared<StringDictColumn>(
            country_dict, country_codes, country_valid);
        *batch = batch->withStringColumn(3001, country_col); // country key_id

        // Add title column: dict ["L1","L2",...,"L{fanout}"], codes 0..fanout-1
        auto title_dict = std::make_shared<std::vector<std::string>>();
        title_dict->reserve(n);
        for (size_t i = 0; i < n; ++i) {
          title_dict->push_back("L" + std::to_string(i + 1));
        }
        auto title_codes = std::make_shared<std::vector<int32_t>>(n);
        auto title_valid = std::make_shared<std::vector<uint8_t>>(n, 1);
        for (size_t i = 0; i < n; ++i) {
          (*title_codes)[i] = static_cast<int32_t>(i);
        }
        auto title_col = std::make_shared<StringDictColumn>(
            title_dict, title_codes, title_valid);
        *batch = batch->withStringColumn(3002, title_col); // title key_id

        return RowSet(std::make_shared<ColumnBatch>(*batch));
      });

  // Register viewer.fetch_cached_recommendation
  TaskSpec fetch_cached_spec{
      .op = "viewer.fetch_cached_recommendation",
      .params_schema =
          {
              {.name = "fanout", .type = TaskParamType::Int, .required = true},
              {.name = "trace",
               .type = TaskParamType::String,
               .required = false,
               .nullable = true},
          },
      .reads = {},
      .writes = {},
      .default_budget = {.timeout_ms = 100},
      .output_pattern = OutputPattern::SourceFanoutDense,
  };

  register_task(
      std::move(fetch_cached_spec),
      [](const std::vector<RowSet> &inputs, const ValidatedParams &params,
         [[maybe_unused]] const ExecCtx &ctx) -> RowSet {
        if (!inputs.empty()) {
          throw std::runtime_error(
              "viewer.fetch_cached_recommendation: expected 0 inputs");
        }
        int64_t fanout = params.get_int("fanout");
        if (fanout <= 0) {
          throw std::runtime_error(
              "viewer.fetch_cached_recommendation: 'fanout' must be > 0");
        }
        constexpr int64_t kMaxFanout = 10'000'000;
        if (fanout > kMaxFanout) {
          throw std::runtime_error("viewer.fetch_cached_recommendation: "
                                   "'fanout' exceeds maximum limit (10000000)");
        }

        size_t n = static_cast<size_t>(fanout);

        // Create ColumnBatch with ids 1001..1000+fanout
        auto batch = std::make_shared<ColumnBatch>(n);
        for (size_t i = 0; i < n; ++i) {
          batch->setId(i, 1001 + static_cast<int64_t>(i));
        }

        // Add country column: dict ["CA","FR"], pattern CA,FR,CA,FR...
        auto country_dict = std::make_shared<std::vector<std::string>>(
            std::vector<std::string>{"CA", "FR"});
        auto country_codes = std::make_shared<std::vector<int32_t>>(n);
        auto country_valid = std::make_shared<std::vector<uint8_t>>(n, 1);
        for (size_t i = 0; i < n; ++i) {
          (*country_codes)[i] = static_cast<int32_t>(i % 2); // 0=CA, 1=FR
        }
        auto country_col = std::make_shared<StringDictColumn>(
            country_dict, country_codes, country_valid);
        *batch = batch->withStringColumn(3001, country_col);

        // No title column for this source

        return RowSet(std::make_shared<ColumnBatch>(*batch));
      });

  // Register take
  TaskSpec take_spec{
      .op = "take",
      .params_schema =
          {
              {.name = "count", .type = TaskParamType::Int, .required = true},
              {.name = "trace",
               .type = TaskParamType::String,
               .required = false,
               .nullable = true},
          },
      .reads = {},
      .writes = {},
      .default_budget = {.timeout_ms = 10},
      .output_pattern = OutputPattern::PrefixOfInput,
  };

  register_task(
      std::move(take_spec),
      [](const std::vector<RowSet> &inputs, const ValidatedParams &params,
         [[maybe_unused]] const ExecCtx &ctx) -> RowSet {
        if (inputs.size() != 1) {
          throw std::runtime_error("take: expected exactly 1 input");
        }
        int64_t count = params.get_int("count");
        if (count <= 0) {
          throw std::runtime_error("take: 'count' must be > 0");
        }

        const auto &input = inputs[0];
        size_t limit = static_cast<size_t>(count);

        // Use truncateTo - shares batch pointer, creates new selection
        return input.truncateTo(limit);
      });

  // Register vm
  TaskSpec vm_spec{
      .op = "vm",
      .params_schema =
          {
              {.name = "out_key", .type = TaskParamType::Int, .required = true},
              {.name = "expr_id",
               .type = TaskParamType::ExprId,
               .required = true},
              {.name = "trace",
               .type = TaskParamType::String,
               .required = false,
               .nullable = true},
          },
      .reads = {},
      .writes = {},
      .default_budget = {.timeout_ms = 50},
      .output_pattern = OutputPattern::UnaryPreserveView,
  };

  register_task(
      std::move(vm_spec),
      [](const std::vector<RowSet> &inputs, const ValidatedParams &params,
         const ExecCtx &ctx) -> RowSet {
        if (inputs.size() != 1) {
          throw std::runtime_error("vm: expected exactly 1 input");
        }

        int64_t out_key_raw = params.get_int("out_key");
        if (out_key_raw <= 0) {
          throw std::runtime_error("vm: 'out_key' must be > 0");
        }
        uint32_t out_key = static_cast<uint32_t>(out_key_raw);

        const std::string &expr_id = params.get_string("expr_id");
        if (expr_id.empty()) {
          throw std::runtime_error("vm: 'expr_id' must be non-empty");
        }

        // Validate out_key exists in key registry
        const KeyMeta *key_meta = findKeyById(out_key);
        if (!key_meta) {
          throw std::runtime_error("vm: out_key " + std::to_string(out_key) +
                                   " not in key registry");
        }

        // out_key must NOT be Key.id (key_id=1)
        if (out_key == 1) {
          throw std::runtime_error("vm: cannot write to Key.id");
        }

        // out_key must have allow_write=true
        if (!key_meta->allow_write) {
          throw std::runtime_error("vm: key '" + std::string(key_meta->name) +
                                   "' is not writable");
        }

        // out_key type must be float for this step
        if (key_meta->type != KeyType::Float) {
          throw std::runtime_error("vm: out_key must be Float type");
        }

        // expr_id must exist in expr_table
        if (!ctx.expr_table) {
          throw std::runtime_error("vm: no expr_table in context");
        }
        auto expr_it = ctx.expr_table->find(expr_id);
        if (expr_it == ctx.expr_table->end()) {
          throw std::runtime_error("vm: expr_id '" + expr_id +
                                   "' not found in expr_table");
        }
        const ExprNode &expr = *expr_it->second;

        const auto &input = inputs[0];
        size_t n = input.batch().size();

        // Create new float column
        auto col = std::make_shared<FloatColumn>(n);

        // Evaluate expression for each active row
        bool has_null_active = false;
        input.activeRows().forEachIndex([&](RowIndex row) {
          ExprResult result = eval_expr(expr, row, input.batch(), ctx);

          if (!result) {
            has_null_active = true;
            // valid stays 0
          } else {
            double val = *result;
            // Check for non-finite
            if (!std::isfinite(val)) {
              throw std::runtime_error(
                  "vm: expression produced non-finite value at row " +
                  std::to_string(row));
            }
            col->values[row] = val;
            col->valid[row] = 1;
          }
        });

        // If out_key is not nullable and any active row is null => error
        if (!key_meta->nullable && has_null_active) {
          throw std::runtime_error("vm: null result for non-nullable key '" +
                                   std::string(key_meta->name) + "'");
        }

        // Create new batch with the float column
        auto new_batch = std::make_shared<ColumnBatch>(
            input.batch().withFloatColumn(out_key, col));

        return input.withBatch(new_batch);
      });

  // Register filter
  TaskSpec filter_spec{
      .op = "filter",
      .params_schema =
          {
              {.name = "pred_id",
               .type = TaskParamType::PredId,
               .required = true},
              {.name = "trace",
               .type = TaskParamType::String,
               .required = false,
               .nullable = true},
          },
      .reads = {},
      .writes = {},
      .default_budget = {.timeout_ms = 50},
      .output_pattern = OutputPattern::StableFilter,
  };

  register_task(
      std::move(filter_spec),
      [](const std::vector<RowSet> &inputs, const ValidatedParams &params,
         const ExecCtx &ctx) -> RowSet {
        if (inputs.size() != 1) {
          throw std::runtime_error("filter: expected exactly 1 input");
        }

        const std::string &pred_id = params.get_string("pred_id");
        if (pred_id.empty()) {
          throw std::runtime_error("filter: 'pred_id' must be non-empty");
        }

        // pred_id must exist in pred_table
        if (!ctx.pred_table) {
          throw std::runtime_error("filter: no pred_table in context");
        }
        auto pred_it = ctx.pred_table->find(pred_id);
        if (pred_it == ctx.pred_table->end()) {
          throw std::runtime_error("filter: pred_id '" + pred_id +
                                   "' not found in pred_table");
        }
        const PredNode &pred = *pred_it->second;

        const auto &input = inputs[0];

        // Build new selection by filtering active rows
        SelectionVector new_selection;
        input.activeRows().forEachIndex([&](RowIndex idx) {
          if (eval_pred(pred, idx, input.batch(), ctx)) {
            new_selection.push_back(idx);
          }
        });

        // Return new RowSet with same batch, updated selection
        return input.withSelectionClearOrder(std::move(new_selection));
      });

  // Register concat
  TaskSpec concat_spec{
      .op = "concat",
      .params_schema =
          {
              {.name = "trace",
               .type = TaskParamType::String,
               .required = false,
               .nullable = true},
          },
      .reads = {},
      .writes = {},
      .default_budget = {.timeout_ms = 50},
      .output_pattern = OutputPattern::ConcatDense,
  };

  register_task(std::move(concat_spec), [](const std::vector<RowSet> &inputs,
                                           [[maybe_unused]] const ValidatedParams &params,
                                           [[maybe_unused]] const ExecCtx &ctx) -> RowSet {
    if (inputs.size() != 2) {
      throw std::runtime_error(
          "Error: op 'concat' expects exactly 2 inputs, got " +
          std::to_string(inputs.size()));
    }

    const auto &lhs = inputs[0];
    const auto &rhs = inputs[1];

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
  });
}

void TaskRegistry::register_task(TaskSpec spec, TaskFn fn) {
  // Governance: no task may declare Key.id in writes
  for (const auto &key : spec.writes) {
    if (key == KeyId::id) {
      throw std::runtime_error("Task '" + spec.op +
                               "' cannot declare Key.id in writes");
    }
  }
  std::string op = spec.op;
  tasks_[op] = TaskEntry{.spec = std::move(spec), .run = std::move(fn)};
}

bool TaskRegistry::has_task(const std::string &op) const {
  return tasks_.find(op) != tasks_.end();
}

const TaskSpec &TaskRegistry::get_spec(const std::string &op) const {
  auto it = tasks_.find(op);
  if (it == tasks_.end()) {
    throw std::runtime_error("Unknown op: " + op);
  }
  return it->second.spec;
}

ValidatedParams
TaskRegistry::validate_params(const std::string &op,
                              const nlohmann::json &params) const {
  const auto &spec = get_spec(op);
  ValidatedParams result;

  // Build set of known param names
  std::unordered_set<std::string> known_params;
  for (const auto &field : spec.params_schema) {
    known_params.insert(field.name);
  }

  // Check for unexpected fields (fail-closed)
  if (params.is_object()) {
    for (auto it = params.begin(); it != params.end(); ++it) {
      if (known_params.find(it.key()) == known_params.end()) {
        throw std::runtime_error("Invalid params for op '" + op +
                                 "': unexpected field '" + it.key() + "'");
      }
    }
  } else if (!params.is_null()) {
    throw std::runtime_error("Invalid params for op '" + op +
                             "': params must be an object or null");
  }

  // Helper to apply default value
  auto apply_default = [&result](const ParamField &field) {
    if (!field.default_value) {
      return;
    }
    const auto &def = *field.default_value;
    switch (field.type) {
    case TaskParamType::Int:
      result.int_params[field.name] = std::get<int64_t>(def);
      break;
    case TaskParamType::Float:
      result.float_params[field.name] = std::get<double>(def);
      break;
    case TaskParamType::Bool:
      result.bool_params[field.name] = std::get<bool>(def);
      break;
    case TaskParamType::String:
    case TaskParamType::ExprId:
    case TaskParamType::PredId:
      result.string_params[field.name] = std::get<std::string>(def);
      break;
    }
  };

  // Validate each field in schema
  for (const auto &field : spec.params_schema) {
    bool present = params.is_object() && params.contains(field.name);
    bool is_null = present && params[field.name].is_null();

    // Handle absent or null values
    if (!present || is_null) {
      if (field.required) {
        throw std::runtime_error("Invalid params for op '" + op +
                                 "': missing required field '" + field.name +
                                 "'");
      }
      if (field.default_value) {
        // Use default value
        apply_default(field);
      } else if (!field.nullable) {
        // Not nullable and no default - fail for null, skip for absent
        if (is_null) {
          throw std::runtime_error("Invalid params for op '" + op +
                                   "': field '" + field.name +
                                   "' cannot be null");
        }
        // Absent optional without default - just skip
      }
      // else: nullable without default - skip (no value in ValidatedParams)
      continue;
    }

    // Value is present and not null - validate type
    const auto &value = params[field.name];

    switch (field.type) {
    case TaskParamType::Int: {
      if (!value.is_number_integer()) {
        // Also check if it's a float that happens to be a whole number
        if (value.is_number_float()) {
          double d = value.get<double>();
          if (std::floor(d) != d || d < std::numeric_limits<int64_t>::min() ||
              d > std::numeric_limits<int64_t>::max()) {
            throw std::runtime_error("Invalid params for op '" + op +
                                     "': field '" + field.name +
                                     "' must be int");
          }
          result.int_params[field.name] = static_cast<int64_t>(d);
        } else {
          throw std::runtime_error("Invalid params for op '" + op +
                                   "': field '" + field.name + "' must be int");
        }
      } else {
        result.int_params[field.name] = value.get<int64_t>();
      }
      break;
    }
    case TaskParamType::Float: {
      if (!value.is_number()) {
        throw std::runtime_error("Invalid params for op '" + op +
                                 "': field '" + field.name + "' must be float");
      }
      result.float_params[field.name] = value.get<double>();
      break;
    }
    case TaskParamType::Bool: {
      if (!value.is_boolean()) {
        throw std::runtime_error("Invalid params for op '" + op +
                                 "': field '" + field.name + "' must be bool");
      }
      result.bool_params[field.name] = value.get<bool>();
      break;
    }
    case TaskParamType::String: {
      if (!value.is_string()) {
        throw std::runtime_error("Invalid params for op '" + op +
                                 "': field '" + field.name +
                                 "' must be string");
      }
      result.string_params[field.name] = value.get<std::string>();
      break;
    }
    case TaskParamType::ExprId:
    case TaskParamType::PredId: {
      // ExprId/PredId are stored as strings; validation against
      // expr_table/pred_table happens in validate_plan
      if (!value.is_string()) {
        throw std::runtime_error("Invalid params for op '" + op +
                                 "': field '" + field.name +
                                 "' must be string");
      }
      result.string_params[field.name] = value.get<std::string>();
      break;
    }
    }
  }

  return result;
}

RowSet TaskRegistry::execute(const std::string &op,
                             const std::vector<RowSet> &inputs,
                             const ValidatedParams &params,
                             const ExecCtx &ctx) const {
  auto it = tasks_.find(op);
  if (it == tasks_.end()) {
    throw std::runtime_error("Unknown op: " + op);
  }
  return it->second.run(inputs, params, ctx);
}

std::vector<TaskSpec> TaskRegistry::get_all_specs() const {
  std::vector<TaskSpec> specs;
  specs.reserve(tasks_.size());
  for (const auto &[op, entry] : tasks_) {
    specs.push_back(entry.spec);
  }
  // Sort by op for deterministic ordering
  std::sort(specs.begin(), specs.end(),
            [](const TaskSpec &a, const TaskSpec &b) { return a.op < b.op; });
  return specs;
}

std::string TaskRegistry::compute_manifest_digest() const {
  // Build canonical JSON
  nlohmann::ordered_json manifest;
  manifest["schema_version"] = 1;

  nlohmann::ordered_json tasks_json = nlohmann::json::array();
  auto specs = get_all_specs();

  for (const auto &spec : specs) {
    nlohmann::ordered_json task_json;
    task_json["op"] = spec.op;

    // Sort params by name
    auto sorted_params = spec.params_schema;
    std::sort(sorted_params.begin(), sorted_params.end(),
              [](const ParamField &a, const ParamField &b) {
                return a.name < b.name;
              });

    nlohmann::ordered_json params_json = nlohmann::json::array();
    for (const auto &p : sorted_params) {
      nlohmann::ordered_json pj;
      // Use alphabetical order for deterministic JSON
      if (p.default_value) {
        std::visit([&pj](const auto &v) { pj["default"] = v; },
                   *p.default_value);
      }
      pj["name"] = p.name;
      pj["nullable"] = p.nullable;
      pj["required"] = p.required;
      switch (p.type) {
      case TaskParamType::Int:
        pj["type"] = "int";
        break;
      case TaskParamType::Float:
        pj["type"] = "float";
        break;
      case TaskParamType::Bool:
        pj["type"] = "bool";
        break;
      case TaskParamType::String:
        pj["type"] = "string";
        break;
      case TaskParamType::ExprId:
        pj["type"] = "expr_id";
        break;
      case TaskParamType::PredId:
        pj["type"] = "pred_id";
        break;
      }
      params_json.push_back(pj);
    }
    task_json["params"] = params_json;

    // reads/writes as sorted arrays of key IDs
    auto sorted_reads = spec.reads;
    std::sort(sorted_reads.begin(), sorted_reads.end());
    nlohmann::ordered_json reads_json = nlohmann::json::array();
    for (const auto &key : sorted_reads) {
      reads_json.push_back(static_cast<uint32_t>(key));
    }
    task_json["reads"] = reads_json;

    auto sorted_writes = spec.writes;
    std::sort(sorted_writes.begin(), sorted_writes.end());
    nlohmann::ordered_json writes_json = nlohmann::json::array();
    for (const auto &key : sorted_writes) {
      writes_json.push_back(static_cast<uint32_t>(key));
    }
    task_json["writes"] = writes_json;

    nlohmann::ordered_json budget;
    budget["timeout_ms"] = spec.default_budget.timeout_ms;
    task_json["default_budget"] = budget;

    // Add output_pattern for deterministic manifest
    task_json["output_pattern"] = outputPatternToString(spec.output_pattern);

    tasks_json.push_back(task_json);
  }

  manifest["tasks"] = tasks_json;

  // Serialize to compact JSON (no whitespace)
  std::string canonical = manifest.dump();
  return sha256::hash(canonical);
}

} // namespace rankd
