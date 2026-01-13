#pragma once

#include "column_batch.h"
#include <algorithm>
#include <memory>
#include <optional>
#include <vector>

namespace rankd {

struct RowSet {
  std::shared_ptr<const ColumnBatch> batch;
  std::optional<std::vector<uint32_t>> selection;
  std::optional<std::vector<uint32_t>> order;

  // Returns up to `limit` row indices in iteration order.
  // Does NOT copy columns - only indices.
  std::vector<uint32_t> materializeIndexViewForOutput(size_t limit) const {
    std::vector<uint32_t> result;
    result.reserve(std::min(limit, batch->size()));

    if (order && selection) {
      // Both exist: iterate order, filter by selection membership
      std::vector<uint8_t> in_selection(batch->size(), 0);
      for (uint32_t idx : *selection) {
        in_selection[idx] = 1;
      }
      for (uint32_t idx : *order) {
        if (in_selection[idx]) {
          result.push_back(idx);
          if (result.size() >= limit)
            break;
        }
      }
    } else if (order) {
      // Only order exists
      for (uint32_t idx : *order) {
        result.push_back(idx);
        if (result.size() >= limit)
          break;
      }
    } else if (selection) {
      // Only selection exists
      for (uint32_t idx : *selection) {
        result.push_back(idx);
        if (result.size() >= limit)
          break;
      }
    } else {
      // Neither: iterate [0..size)
      size_t n = std::min(limit, batch->size());
      for (size_t i = 0; i < n; ++i) {
        result.push_back(static_cast<uint32_t>(i));
      }
    }

    return result;
  }

  // Returns the logical size (number of active rows)
  size_t logicalSize() const {
    if (order && selection) {
      // Count how many order entries are in selection
      std::vector<uint8_t> in_selection(batch->size(), 0);
      for (uint32_t idx : *selection) {
        in_selection[idx] = 1;
      }
      size_t count = 0;
      for (uint32_t idx : *order) {
        if (in_selection[idx])
          count++;
      }
      return count;
    } else if (order) {
      return order->size();
    } else if (selection) {
      return selection->size();
    } else {
      return batch->size();
    }
  }
};

} // namespace rankd
