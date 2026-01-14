#pragma once

#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace rankd {

struct DebugCounters {
  uint64_t materialize_count = 0;
};

// Float column storage: values + validity bitmap
struct FloatColumn {
  std::vector<double> values;
  std::vector<uint8_t> valid;

  explicit FloatColumn(size_t n) : values(n, 0.0), valid(n, 0) {}
};

// String dictionary column: dictionary-encoded strings
// dict contains unique strings, codes index into dict, valid is bitmap
struct StringDictColumn {
  std::shared_ptr<const std::vector<std::string>> dict;
  std::shared_ptr<const std::vector<int32_t>> codes; // length N
  std::shared_ptr<const std::vector<uint8_t>> valid; // length N (1/0)

  StringDictColumn(std::shared_ptr<const std::vector<std::string>> d,
                   std::shared_ptr<const std::vector<int32_t>> c,
                   std::shared_ptr<const std::vector<uint8_t>> v)
      : dict(std::move(d)), codes(std::move(c)), valid(std::move(v)) {}
};

// Shared id column storage (allows sharing without copy)
struct IdColumn {
  std::vector<int64_t> values;
  std::vector<uint8_t> valid;

  explicit IdColumn(size_t n) : values(n), valid(n, 1) {}
};

class ColumnBatch {
public:
  explicit ColumnBatch(size_t num_rows,
                       std::shared_ptr<DebugCounters> debug = nullptr)
      : id_col_(std::make_shared<IdColumn>(num_rows)),
        debug_(debug ? debug : std::make_shared<DebugCounters>()) {}

  size_t size() const { return id_col_->values.size(); }

  int64_t getId(size_t row_index) const { return id_col_->values[row_index]; }

  void setId(size_t row_index, int64_t value) {
    id_col_->values[row_index] = value;
  }

  bool isIdValid(size_t row_index) const {
    return id_col_->valid[row_index] != 0;
  }

  const std::shared_ptr<DebugCounters> &debug() const { return debug_; }

  // Copy id column - increments materialize_count
  std::vector<int64_t> copyIdColumn() const {
    debug_->materialize_count++;
    return id_col_->values;
  }

  // Float column accessors
  bool hasFloat(uint32_t key_id) const {
    return float_cols_.find(key_id) != float_cols_.end();
  }

  const FloatColumn *getFloatCol(uint32_t key_id) const {
    auto it = float_cols_.find(key_id);
    if (it == float_cols_.end()) {
      return nullptr;
    }
    return it->second.get();
  }

  // Returns a NEW ColumnBatch that shares the same id storage and existing
  // columns, but adds/replaces the specified float column.
  // Does NOT increment materialize_count (no id copy).
  ColumnBatch withFloatColumn(uint32_t key_id,
                              std::shared_ptr<const FloatColumn> col) const {
    ColumnBatch result;
    result.id_col_ = id_col_;           // Share id storage
    result.float_cols_ = float_cols_;   // Copy map (shared_ptr copies are cheap)
    result.string_cols_ = string_cols_; // Share string columns
    result.debug_ = debug_;             // Share debug counters
    result.float_cols_[key_id] = std::move(col);
    return result;
  }

  // Get all float column key_ids in ascending order (for deterministic output)
  std::vector<uint32_t> getFloatKeyIds() const {
    std::vector<uint32_t> keys;
    keys.reserve(float_cols_.size());
    for (const auto &[key_id, col] : float_cols_) {
      keys.push_back(key_id);
    }
    return keys;
  }

  // String column accessors
  bool hasString(uint32_t key_id) const {
    return string_cols_.find(key_id) != string_cols_.end();
  }

  const StringDictColumn *getStringCol(uint32_t key_id) const {
    auto it = string_cols_.find(key_id);
    if (it == string_cols_.end()) {
      return nullptr;
    }
    return it->second.get();
  }

  // Returns a NEW ColumnBatch that shares the same id storage and existing
  // columns, but adds/replaces the specified string column.
  ColumnBatch
  withStringColumn(uint32_t key_id,
                   std::shared_ptr<const StringDictColumn> col) const {
    ColumnBatch result;
    result.id_col_ = id_col_;           // Share id storage
    result.float_cols_ = float_cols_;   // Share float columns
    result.string_cols_ = string_cols_; // Copy map (shared_ptr copies are cheap)
    result.debug_ = debug_;             // Share debug counters
    result.string_cols_[key_id] = std::move(col);
    return result;
  }

  // Get all string column key_ids in ascending order (for deterministic output)
  std::vector<uint32_t> getStringKeyIds() const {
    std::vector<uint32_t> keys;
    keys.reserve(string_cols_.size());
    for (const auto &[key_id, col] : string_cols_) {
      keys.push_back(key_id);
    }
    return keys;
  }

private:
  // Private default constructor for with*Column
  ColumnBatch() = default;

  std::shared_ptr<IdColumn> id_col_;
  std::map<uint32_t, std::shared_ptr<const FloatColumn>>
      float_cols_; // key_id -> column
  std::map<uint32_t, std::shared_ptr<const StringDictColumn>>
      string_cols_; // key_id -> column
  std::shared_ptr<DebugCounters> debug_;
};

} // namespace rankd
