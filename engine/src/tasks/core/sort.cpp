#include "expr_eval.h"
#include "task_registry.h"
#include <algorithm>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace rankd {

class SortTask {
public:
  static TaskSpec spec() {
    return TaskSpec{
        .op = "sort",
        .params_schema = {
            {.name = "by", .type = TaskParamType::Int, .required = true},
            {.name = "order",
             .type = TaskParamType::String,
             .required = false,
             .nullable = false,
             .default_value = std::string("asc")},
            {.name = "trace",
             .type = TaskParamType::String,
             .required = false,
             .nullable = true},
        },
        .reads = {},
        .writes = {},
        .default_budget = {.timeout_ms = 50},
        .output_pattern = OutputPattern::PermutationOfInput,
        // writes_effect omitted - no column writes
    };
  }

  static RowSet run(const std::vector<RowSet> &inputs,
                    const ValidatedParams &params,
                    [[maybe_unused]] const ExecCtx &ctx) {
    if (inputs.size() != 1) {
      throw std::runtime_error("sort: expected exactly 1 input");
    }
    const auto &input = inputs[0];

    int64_t by_raw = params.get_int("by");
    if (by_raw <= 0) {
      throw std::runtime_error("sort: 'by' must be > 0");
    }
    uint32_t by_key = static_cast<uint32_t>(by_raw);

    std::string order = params.has_string("order") ? params.get_string("order")
                                                   : std::string("asc");
    bool ascending;
    if (order == "asc") {
      ascending = true;
    } else if (order == "desc") {
      ascending = false;
    } else {
      throw std::runtime_error(
          "sort: 'order' must be 'asc' or 'desc' if provided");
    }

    const KeyMeta *meta = findKeyById(by_key);
    if (!meta) {
      throw std::runtime_error("sort: key " + std::to_string(by_key) +
                               " not in key registry");
    }
    if (!meta->allow_read) {
      throw std::runtime_error("sort: key '" + std::string(meta->name) +
                               "' is not readable");
    }
    if (meta->status == Status::Blocked) {
      throw std::runtime_error("sort: key '" + std::string(meta->name) +
                               "' is blocked");
    }

    // Gather active rows in current iteration order for stable sorting.
    Permutation active_rows = input.activeRows().toVector(input.rowCount());

    auto null_first_cmp = [ascending](bool a_null,
                                      bool b_null) -> std::optional<bool> {
      if (a_null && !b_null) {
        return false; // nulls go last
      }
      if (!a_null && b_null) {
        return true; // non-null before null
      }
      // both null or both non-null
      return std::nullopt;
    };

    switch (meta->type) {
    case KeyType::Int: {
      // Only Key.id is materialized as int
      if (by_key != key_id(KeyId::id)) {
        throw std::runtime_error("sort: key '" + std::string(meta->name) +
                                 "' is not sortable (int columns not stored)");
      }
      auto comp = [&](RowIndex a, RowIndex b) {
        bool a_null = !input.batch().isIdValid(a);
        bool b_null = !input.batch().isIdValid(b);
        auto null_cmp = null_first_cmp(a_null, b_null);
        if (null_cmp.has_value()) {
          return *null_cmp;
        }
        if (a_null && b_null) {
          return false; // both null: treat as equal
        }
        int64_t av = input.batch().getId(a);
        int64_t bv = input.batch().getId(b);
        if (av == bv) {
          return false;
        }
        return ascending ? av < bv : av > bv;
      };
      std::stable_sort(active_rows.begin(), active_rows.end(), comp);
      break;
    }

    case KeyType::Float: {
      const FloatColumn *col = input.batch().getFloatCol(by_key);
      if (!col) {
        throw std::runtime_error("sort: column for key '" +
                                 std::string(meta->name) + "' not found");
      }
      auto comp = [&](RowIndex a, RowIndex b) {
        bool a_null = col->valid[a] == 0;
        bool b_null = col->valid[b] == 0;
        auto null_cmp = null_first_cmp(a_null, b_null);
        if (null_cmp.has_value()) {
          return *null_cmp;
        }
        if (a_null && b_null) {
          return false; // both null: treat as equal
        }
        double av = col->values[a];
        double bv = col->values[b];
        if (av == bv) {
          return false;
        }
        return ascending ? av < bv : av > bv;
      };
      std::stable_sort(active_rows.begin(), active_rows.end(), comp);
      break;
    }

    case KeyType::String: {
      const StringDictColumn *col = input.batch().getStringCol(by_key);
      if (!col) {
        throw std::runtime_error("sort: column for key '" +
                                 std::string(meta->name) + "' not found");
      }
      auto string_at = [&](RowIndex idx) -> std::optional<std::string_view> {
        if ((*col->valid)[idx] == 0) {
          return std::nullopt;
        }
        int32_t code = (*col->codes)[idx];
        if (code < 0 || static_cast<size_t>(code) >= col->dict->size()) {
          throw std::runtime_error("sort: invalid string code for key '" +
                                   std::string(meta->name) + "'");
        }
        return std::string_view(col->dict->at(static_cast<size_t>(code)));
      };

      auto comp = [&](RowIndex a, RowIndex b) {
        auto a_val = string_at(a);
        auto b_val = string_at(b);
        bool a_null = !a_val.has_value();
        bool b_null = !b_val.has_value();
        auto null_cmp = null_first_cmp(a_null, b_null);
        if (null_cmp.has_value()) {
          return *null_cmp;
        }
        if (a_null && b_null) {
          return false; // both null: treat as equal
        }
        if (*a_val == *b_val) {
          return false;
        }
        return ascending ? *a_val < *b_val : *a_val > *b_val;
      };

      std::stable_sort(active_rows.begin(), active_rows.end(), comp);
      break;
    }

    case KeyType::Bool:
    case KeyType::FeatureBundle: {
      throw std::runtime_error("sort: key '" + std::string(meta->name) +
                               "' is not sortable");
    }
    }

    return input.withOrder(std::move(active_rows));
  }
};

// Auto-register this task with namespace
REGISTER_TASK(SortTask);

} // namespace rankd
