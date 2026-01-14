#include "output_contract.h"
#include "task_registry.h"
#include <sstream>
#include <stdexcept>

namespace rankd {

const char *outputPatternToString(OutputPattern pattern) {
  switch (pattern) {
  case OutputPattern::SourceFanoutDense:
    return "SourceFanoutDense";
  case OutputPattern::UnaryPreserveView:
    return "UnaryPreserveView";
  case OutputPattern::StableFilter:
    return "StableFilter";
  case OutputPattern::PrefixOfInput:
    return "PrefixOfInput";
  case OutputPattern::ConcatDense:
    return "ConcatDense";
  }
  return "Unknown";
}

// Helper: check if output active rows are dense [0..N)
static bool isDenseActive(const RowSet &rs) {
  size_t expected = rs.rowCount();
  size_t count = 0;
  bool ok = true;
  rs.activeRows().forEachIndex([&](RowIndex idx) -> bool {
    if (idx != count) {
      ok = false;
      return false; // stop
    }
    count++;
    return true;
  });
  return ok && count == expected;
}

// Helper: check if 'output' activeRows equals 'input' activeRows exactly
static bool activeRowsEqual(const RowSet &input, const RowSet &output) {
  // Compare sequences
  auto inActive = input.activeRows().toVector(input.rowCount());
  auto outActive = output.activeRows().toVector(output.rowCount());
  return inActive == outActive;
}

// Helper: check if 'output' activeRows is a subsequence of 'input' activeRows
static bool isSubsequence(const RowSet &input, const RowSet &output) {
  auto inActive = input.activeRows().toVector(input.rowCount());
  auto outActive = output.activeRows().toVector(output.rowCount());

  size_t j = 0; // index into outActive
  for (size_t i = 0; i < inActive.size() && j < outActive.size(); ++i) {
    if (inActive[i] == outActive[j]) {
      ++j;
    }
  }
  return j == outActive.size();
}

// Helper: check if 'output' activeRows is prefix of 'input' activeRows
static bool isPrefix(const RowSet &input, const RowSet &output,
                     size_t expected_count) {
  auto inActive = input.activeRows().toVector(input.rowCount());
  auto outActive = output.activeRows().toVector(output.rowCount());

  size_t k = std::min(expected_count, inActive.size());
  if (outActive.size() != k) {
    return false;
  }
  for (size_t i = 0; i < k; ++i) {
    if (inActive[i] != outActive[i]) {
      return false;
    }
  }
  return true;
}

void validateTaskOutput(const std::string &node_id, const std::string &op,
                        OutputPattern pattern,
                        const std::vector<RowSet> &inputs,
                        const ValidatedParams &params, const RowSet &output) {
  auto makeError = [&](const std::string &detail) {
    std::ostringstream oss;
    oss << "Error: Node '" << node_id << "': op '" << op
        << "' violated output contract: " << detail;
    throw std::runtime_error(oss.str());
  };

  switch (pattern) {
  case OutputPattern::SourceFanoutDense: {
    // Expected rowCount = params.fanout
    if (!params.has_int("fanout")) {
      makeError("SourceFanoutDense requires 'fanout' param");
    }
    size_t expected = static_cast<size_t>(params.get_int("fanout"));
    if (output.rowCount() != expected) {
      std::ostringstream oss;
      oss << "expected out.rowCount=" << expected
          << " (SourceFanoutDense), got " << output.rowCount();
      makeError(oss.str());
    }
    // Active rows must be dense [0..N)
    if (!isDenseActive(output)) {
      makeError("SourceFanoutDense requires dense active rows [0..N)");
    }
    break;
  }

  case OutputPattern::UnaryPreserveView: {
    if (inputs.empty()) {
      makeError("UnaryPreserveView requires at least 1 input");
    }
    if (output.rowCount() != inputs[0].rowCount()) {
      std::ostringstream oss;
      oss << "expected out.rowCount=" << inputs[0].rowCount()
          << " (UnaryPreserveView), got " << output.rowCount();
      makeError(oss.str());
    }
    if (!activeRowsEqual(inputs[0], output)) {
      makeError(
          "UnaryPreserveView requires output activeRows to match input[0]");
    }
    break;
  }

  case OutputPattern::StableFilter: {
    if (inputs.empty()) {
      makeError("StableFilter requires at least 1 input");
    }
    if (output.rowCount() != inputs[0].rowCount()) {
      std::ostringstream oss;
      oss << "expected out.rowCount=" << inputs[0].rowCount()
          << " (StableFilter), got " << output.rowCount();
      makeError(oss.str());
    }
    if (!isSubsequence(inputs[0], output)) {
      makeError("StableFilter requires output activeRows to be subsequence of "
                "input[0]");
    }
    break;
  }

  case OutputPattern::PrefixOfInput: {
    if (inputs.empty()) {
      makeError("PrefixOfInput requires at least 1 input");
    }
    if (!params.has_int("count")) {
      makeError("PrefixOfInput requires 'count' param");
    }
    size_t count_param = static_cast<size_t>(params.get_int("count"));
    size_t input_active_size = inputs[0].logicalSize();
    size_t expected_k = std::min(count_param, input_active_size);

    if (output.rowCount() != inputs[0].rowCount()) {
      std::ostringstream oss;
      oss << "expected out.rowCount=" << inputs[0].rowCount()
          << " (PrefixOfInput), got " << output.rowCount();
      makeError(oss.str());
    }
    if (!isPrefix(inputs[0], output, expected_k)) {
      std::ostringstream oss;
      oss << "PrefixOfInput requires output activeRows to be first " << expected_k
          << " of input[0] activeRows";
      makeError(oss.str());
    }
    break;
  }

  case OutputPattern::ConcatDense: {
    if (inputs.size() != 2) {
      std::ostringstream oss;
      oss << "ConcatDense requires exactly 2 inputs, got " << inputs.size();
      makeError(oss.str());
    }
    size_t expected =
        inputs[0].logicalSize() + inputs[1].logicalSize();
    if (output.rowCount() != expected) {
      std::ostringstream oss;
      oss << "expected out.rowCount=" << expected << " (ConcatDense), got "
          << output.rowCount();
      makeError(oss.str());
    }
    // Active rows must be dense [0..N)
    if (!isDenseActive(output)) {
      makeError("ConcatDense requires dense active rows [0..N)");
    }
    break;
  }
  }
}

} // namespace rankd
