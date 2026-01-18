#pragma once

#include "rowset.h"
#include <algorithm>
#include <cstdint>
#include <vector>

namespace rankd {

// Schema delta computed after each node execution
// Records what columns appeared/disappeared compared to inputs
struct SchemaDelta {
  std::vector<uint32_t> in_keys_union;  // sorted, unique - union of all input keys
  std::vector<uint32_t> out_keys;       // sorted, unique - keys in output
  std::vector<uint32_t> new_keys;       // out - in_union (keys added by this node)
  std::vector<uint32_t> removed_keys;   // in_union - out (keys removed by this node)
};

// Per-node schema delta with node_id for tracing
struct NodeSchemaDelta {
  std::string node_id;
  SchemaDelta delta;
};

// Collect all keys (float + string columns) from a ColumnBatch
// Returns sorted, unique vector of key IDs
// NOTE: Only covers float/string columns. Extend when adding new column types
// (e.g., feature bundles, bool columns).
inline std::vector<uint32_t> collect_keys(const ColumnBatch &batch) {
  std::vector<uint32_t> keys;

  // Get float column keys
  auto float_keys = batch.getFloatKeyIds();
  keys.insert(keys.end(), float_keys.begin(), float_keys.end());

  // Get string column keys
  auto string_keys = batch.getStringKeyIds();
  keys.insert(keys.end(), string_keys.begin(), string_keys.end());

  // Sort and dedupe
  std::sort(keys.begin(), keys.end());
  keys.erase(std::unique(keys.begin(), keys.end()), keys.end());

  return keys;
}

// Compute union of two sorted, unique key vectors
// Returns sorted, unique result
inline std::vector<uint32_t> union_keys(const std::vector<uint32_t> &a,
                                        const std::vector<uint32_t> &b) {
  std::vector<uint32_t> result;
  result.reserve(a.size() + b.size());
  std::set_union(a.begin(), a.end(), b.begin(), b.end(),
                 std::back_inserter(result));
  return result;
}

// Compute set difference: a - b (elements in a but not in b)
// Both inputs must be sorted, unique
// Returns sorted, unique result
inline std::vector<uint32_t> set_diff(const std::vector<uint32_t> &a,
                                      const std::vector<uint32_t> &b) {
  std::vector<uint32_t> result;
  result.reserve(a.size());
  std::set_difference(a.begin(), a.end(), b.begin(), b.end(),
                      std::back_inserter(result));
  return result;
}

// Compute schema delta for a node execution
// inputs: the input RowSets (0, 1, or 2)
// output: the output RowSet
inline SchemaDelta compute_schema_delta(
    const std::vector<RowSet> &inputs,
    const RowSet &output) {
  SchemaDelta delta;

  // Compute input key union based on number of inputs
  if (inputs.empty()) {
    // Source node: no input keys
    delta.in_keys_union = {};
  } else if (inputs.size() == 1) {
    // Unary node
    delta.in_keys_union = collect_keys(inputs[0].batch());
  } else if (inputs.size() == 2) {
    // Binary node
    auto lhs_keys = collect_keys(inputs[0].batch());
    auto rhs_keys = collect_keys(inputs[1].batch());
    delta.in_keys_union = union_keys(lhs_keys, rhs_keys);
  } else {
    // N-ary node (generalize to union of all)
    for (const auto &inp : inputs) {
      auto inp_keys = collect_keys(inp.batch());
      delta.in_keys_union = union_keys(delta.in_keys_union, inp_keys);
    }
  }

  // Compute output keys
  delta.out_keys = collect_keys(output.batch());

  // Compute deltas
  delta.new_keys = set_diff(delta.out_keys, delta.in_keys_union);
  delta.removed_keys = set_diff(delta.in_keys_union, delta.out_keys);

  return delta;
}

// Fast path check: if unary and same batch pointer, schema is unchanged
inline bool is_same_batch(const std::vector<RowSet> &inputs,
                          const RowSet &output) {
  if (inputs.size() == 1) {
    return inputs[0].batchPtr().get() == output.batchPtr().get();
  }
  return false;
}

} // namespace rankd
