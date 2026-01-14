#pragma once

#include "column_batch.h"
#include <algorithm>
#include <memory>
#include <optional>
#include <type_traits>
#include <vector>

namespace rankd {

// Type aliases for clarity
using RowIndex = uint32_t;
using SelectionVector = std::vector<RowIndex>;
using Permutation = std::vector<RowIndex>;

// Forward declaration
class RowSet;

// View class for iterating over active rows in a RowSet
// Does not own any data - lifetime must not exceed the RowSet it references
class ActiveRows {
public:
  ActiveRows(const ColumnBatch *batch,
             const std::optional<SelectionVector> *selection,
             const std::optional<Permutation> *order)
      : batch_(batch), selection_(selection), order_(order) {}

  // Iterate over active row indices, calling fn(RowIndex) for each.
  // If fn returns false, iteration stops early. If fn returns void, iteration continues.
  template <typename Fn> void forEachIndex(Fn &&fn) const {
    // Helper to handle both void and bool return types
    auto call_fn = [&](RowIndex idx) -> bool {
      if constexpr (std::is_same_v<decltype(fn(idx)), void>) {
        fn(idx);
        return true; // continue
      } else {
        return fn(idx); // return value controls continuation
      }
    };

    if (*order_ && *selection_) {
      // Both exist: iterate order, filter by selection membership
      std::vector<uint8_t> in_selection(batch_->size(), 0);
      for (RowIndex idx : **selection_) {
        in_selection[idx] = 1;
      }
      for (RowIndex idx : **order_) {
        if (in_selection[idx]) {
          if (!call_fn(idx))
            return;
        }
      }
    } else if (*order_) {
      // Only order exists
      for (RowIndex idx : **order_) {
        if (!call_fn(idx))
          return;
      }
    } else if (*selection_) {
      // Only selection exists
      for (RowIndex idx : **selection_) {
        if (!call_fn(idx))
          return;
      }
    } else {
      // Neither: iterate [0..size)
      for (size_t i = 0; i < batch_->size(); ++i) {
        if (!call_fn(static_cast<RowIndex>(i)))
          return;
      }
    }
  }

  // Get up to `limit` row indices as a vector
  std::vector<RowIndex> toVector(size_t limit) const {
    std::vector<RowIndex> result;
    result.reserve(std::min(limit, batch_->size()));

    forEachIndex([&](RowIndex idx) -> bool {
      result.push_back(idx);
      return result.size() < limit; // false stops iteration
    });

    return result;
  }

  // Get the number of active rows
  size_t size() const {
    if (*order_ && *selection_) {
      // Count how many order entries are in selection
      std::vector<uint8_t> in_selection(batch_->size(), 0);
      for (RowIndex idx : **selection_) {
        in_selection[idx] = 1;
      }
      size_t count = 0;
      for (RowIndex idx : **order_) {
        if (in_selection[idx])
          count++;
      }
      return count;
    } else if (*order_) {
      return (*order_)->size();
    } else if (*selection_) {
      return (*selection_)->size();
    } else {
      return batch_->size();
    }
  }

private:
  const ColumnBatch *batch_;
  const std::optional<SelectionVector> *selection_;
  const std::optional<Permutation> *order_;
};

// RowSet: a view over a ColumnBatch with optional selection and ordering
// Selection = which rows are active (filtered set)
// Order = iteration order (permutation)
class RowSet {
public:
  // Construct with just a batch (all rows active, natural order)
  explicit RowSet(std::shared_ptr<const ColumnBatch> batch)
      : batch_(std::move(batch)), selection_(std::nullopt), order_(std::nullopt) {
  }

  // Access the underlying batch (read-only)
  const ColumnBatch &batch() const { return *batch_; }

  // Get shared pointer to batch (for sharing between RowSets)
  std::shared_ptr<const ColumnBatch> batchPtr() const { return batch_; }

  // Physical row count (batch size, not active row count)
  size_t rowCount() const { return batch_->size(); }

  // Get a view for iterating over active rows
  ActiveRows activeRows() const { return ActiveRows(batch_.get(), &selection_, &order_); }

  // Returns up to `limit` row indices in iteration order (convenience wrapper)
  std::vector<RowIndex> materializeIndexViewForOutput(size_t limit) const {
    return activeRows().toVector(limit);
  }

  // Returns the logical size (number of active rows)
  size_t logicalSize() const { return activeRows().size(); }

  // Builder: create new RowSet with a different batch
  RowSet withBatch(std::shared_ptr<const ColumnBatch> newBatch) const {
    RowSet result(std::move(newBatch));
    result.selection_ = selection_;
    result.order_ = order_;
    return result;
  }

  // Builder: create new RowSet with a selection vector
  RowSet withSelection(SelectionVector sel) const {
    RowSet result(batch_);
    result.selection_ = std::move(sel);
    result.order_ = order_;
    return result;
  }

  // Builder: create new RowSet with a selection, clearing order
  RowSet withSelectionClearOrder(SelectionVector sel) const {
    RowSet result(batch_);
    result.selection_ = std::move(sel);
    result.order_ = std::nullopt;
    return result;
  }

  // Builder: create new RowSet with an order vector
  RowSet withOrder(Permutation ord) const {
    RowSet result(batch_);
    result.selection_ = selection_;
    result.order_ = std::move(ord);
    return result;
  }

  // Builder: truncate to at most `limit` active rows
  // Materializes active indices and creates a new selection
  RowSet truncateTo(size_t limit) const {
    auto indices = activeRows().toVector(limit);
    RowSet result(batch_);
    result.selection_ = std::move(indices);
    result.order_ = std::nullopt; // Order is baked into the new selection
    return result;
  }

  // Check if selection is present
  bool hasSelection() const { return selection_.has_value(); }

  // Check if order is present
  bool hasOrder() const { return order_.has_value(); }

private:
  std::shared_ptr<const ColumnBatch> batch_;
  std::optional<SelectionVector> selection_;
  std::optional<Permutation> order_;
};

} // namespace rankd
