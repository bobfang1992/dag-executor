#pragma once

#include <cstdint>
#include <memory>
#include <vector>

namespace rankd {

struct DebugCounters {
  uint64_t materialize_count = 0;
};

class ColumnBatch {
public:
  explicit ColumnBatch(size_t num_rows,
                       std::shared_ptr<DebugCounters> debug = nullptr)
      : id_values_(num_rows), id_valid_(num_rows, 1),
        debug_(debug ? debug : std::make_shared<DebugCounters>()) {}

  size_t size() const { return id_values_.size(); }

  int64_t getId(size_t row_index) const { return id_values_[row_index]; }

  void setId(size_t row_index, int64_t value) { id_values_[row_index] = value; }

  bool isIdValid(size_t row_index) const { return id_valid_[row_index] != 0; }

  const std::shared_ptr<DebugCounters> &debug() const { return debug_; }

  // Copy id column - increments materialize_count
  std::vector<int64_t> copyIdColumn() const {
    debug_->materialize_count++;
    return id_values_;
  }

private:
  std::vector<int64_t> id_values_;
  std::vector<uint8_t> id_valid_;
  std::shared_ptr<DebugCounters> debug_;
};

} // namespace rankd
