// Regex tests: simple main() binary (no Catch2)
// Tests dictionary optimization: regex_re2_calls == dict_size, not row_count

#include "column_batch.h"
#include "param_table.h"
#include "plan.h"
#include "pred_eval.h"
#include "task_registry.h"
#include <cassert>
#include <iostream>

using namespace rankd;

void test_literal_pattern() {
  std::cout << "Test: regex with literal pattern... " << std::flush;

  // Clear regex cache to get accurate stats
  clearRegexCache();

  auto &registry = TaskRegistry::instance();

  // Create viewer.follow with fanout=100
  nlohmann::json follow_params;
  follow_params["fanout"] = 100;
  auto fp = registry.validate_params("viewer.follow", follow_params);

  ParamTable params;
  ExecStats stats;
  ExecCtx ctx;
  ctx.params = &params;
  ctx.stats = &stats;

  RowSet source = registry.execute("viewer.follow", {}, fp, ctx);

  // Build regex predicate: Key.country matches "US"
  PredNode pred;
  pred.op = "regex";
  pred.regex_key_id = 3001; // Key.country
  pred.regex_pattern = "US";
  pred.regex_param_id = 0;
  pred.regex_flags = "";

  // Count matches
  size_t match_count = 0;
  source.activeRows().forEachIndex([&](RowIndex idx) {
    if (eval_pred(pred, idx, source.batch(), ctx)) {
      match_count++;
    }
  });

  // country dict is ["US", "CA"], alternating per row
  // So 50 rows should match "US"
  assert(match_count == 50 && "Expected 50 rows matching 'US'");

  // KEY OPTIMIZATION TEST: regex_re2_calls should be dict_size (2), not row_count (100)
  assert(stats.regex_re2_calls == 2 &&
         "Expected 2 RE2 calls (dict size), not 100 (row count)");

  std::cout << "PASS" << std::endl;
}

void test_param_pattern() {
  std::cout << "Test: regex with param_ref pattern... " << std::flush;

  // Clear regex cache to get accurate stats
  clearRegexCache();

  auto &registry = TaskRegistry::instance();

  // Create viewer.follow with fanout=100
  nlohmann::json follow_params;
  follow_params["fanout"] = 100;
  auto fp = registry.validate_params("viewer.follow", follow_params);

  // Set blocklist_regex param to "CA"
  ParamTable params;
  params.set(ParamId::blocklist_regex, std::string("CA"));

  ExecStats stats;
  ExecCtx ctx;
  ctx.params = &params;
  ctx.stats = &stats;

  RowSet source = registry.execute("viewer.follow", {}, fp, ctx);

  // Build regex predicate: Key.country matches param blocklist_regex (id=2)
  PredNode pred;
  pred.op = "regex";
  pred.regex_key_id = 3001; // Key.country
  pred.regex_param_id = 2;  // blocklist_regex
  pred.regex_flags = "";

  // Count matches
  size_t match_count = 0;
  source.activeRows().forEachIndex([&](RowIndex idx) {
    if (eval_pred(pred, idx, source.batch(), ctx)) {
      match_count++;
    }
  });

  // 50 rows should match "CA"
  assert(match_count == 50 && "Expected 50 rows matching 'CA'");

  // KEY OPTIMIZATION TEST: regex_re2_calls should be dict_size (2), not row_count (100)
  assert(stats.regex_re2_calls == 2 &&
         "Expected 2 RE2 calls (dict size), not 100 (row count)");

  std::cout << "PASS" << std::endl;
}

void test_case_insensitive() {
  std::cout << "Test: regex with case-insensitive flag... " << std::flush;

  // Clear regex cache
  clearRegexCache();

  auto &registry = TaskRegistry::instance();

  nlohmann::json follow_params;
  follow_params["fanout"] = 10;
  auto fp = registry.validate_params("viewer.follow", follow_params);

  ParamTable params;
  ExecStats stats;
  ExecCtx ctx;
  ctx.params = &params;
  ctx.stats = &stats;

  RowSet source = registry.execute("viewer.follow", {}, fp, ctx);

  // Build regex predicate: Key.country matches "us" (lowercase) with "i" flag
  PredNode pred;
  pred.op = "regex";
  pred.regex_key_id = 3001; // Key.country
  pred.regex_pattern = "us"; // lowercase
  pred.regex_param_id = 0;
  pred.regex_flags = "i"; // case-insensitive

  size_t match_count = 0;
  source.activeRows().forEachIndex([&](RowIndex idx) {
    if (eval_pred(pred, idx, source.batch(), ctx)) {
      match_count++;
    }
  });

  // Should match "US" rows (5 out of 10)
  assert(match_count == 5 && "Expected 5 rows matching 'us' case-insensitive");

  std::cout << "PASS" << std::endl;
}

void test_null_row_returns_false() {
  std::cout << "Test: regex on null row returns false... " << std::flush;

  // Clear regex cache
  clearRegexCache();

  // Create a batch with one row where string column is null
  auto batch = std::make_shared<ColumnBatch>(1);

  auto dict =
      std::make_shared<std::vector<std::string>>(std::vector<std::string>{"US"});
  auto codes = std::make_shared<std::vector<int32_t>>(std::vector<int32_t>{0});
  auto valid =
      std::make_shared<std::vector<uint8_t>>(std::vector<uint8_t>{0}); // NULL!
  auto col = std::make_shared<StringDictColumn>(dict, codes, valid);
  *batch = batch->withStringColumn(3001, col);

  PredNode pred;
  pred.op = "regex";
  pred.regex_key_id = 3001;
  pred.regex_pattern = "US";
  pred.regex_param_id = 0;
  pred.regex_flags = "";

  ParamTable params;
  ExecCtx ctx;
  ctx.params = &params;

  bool result = eval_pred(pred, 0, *batch, ctx);
  assert(result == false && "Expected null row to return false");

  std::cout << "PASS" << std::endl;
}

void test_missing_column_returns_false() {
  std::cout << "Test: regex on missing column returns false... " << std::flush;

  // Clear regex cache
  clearRegexCache();

  // Create a batch without the string column
  auto batch = std::make_shared<ColumnBatch>(1);

  PredNode pred;
  pred.op = "regex";
  pred.regex_key_id = 3001; // Column doesn't exist
  pred.regex_pattern = "US";
  pred.regex_param_id = 0;
  pred.regex_flags = "";

  ParamTable params;
  ExecCtx ctx;
  ctx.params = &params;

  bool result = eval_pred(pred, 0, *batch, ctx);
  assert(result == false && "Expected missing column to return false");

  std::cout << "PASS" << std::endl;
}

void test_missing_param_throws() {
  std::cout << "Test: regex with missing param throws... " << std::flush;

  // Clear regex cache
  clearRegexCache();

  auto batch = std::make_shared<ColumnBatch>(1);

  auto dict =
      std::make_shared<std::vector<std::string>>(std::vector<std::string>{"US"});
  auto codes = std::make_shared<std::vector<int32_t>>(std::vector<int32_t>{0});
  auto valid = std::make_shared<std::vector<uint8_t>>(std::vector<uint8_t>{1});
  auto col = std::make_shared<StringDictColumn>(dict, codes, valid);
  *batch = batch->withStringColumn(3001, col);

  PredNode pred;
  pred.op = "regex";
  pred.regex_key_id = 3001;
  pred.regex_param_id = 2; // blocklist_regex - not set!
  pred.regex_flags = "";

  ParamTable params; // No blocklist_regex set
  ExecCtx ctx;
  ctx.params = &params;

  bool threw = false;
  try {
    eval_pred(pred, 0, *batch, ctx);
  } catch (const std::runtime_error &e) {
    threw = true;
    // Verify error message mentions param
    std::string msg = e.what();
    assert(msg.find("param") != std::string::npos &&
           "Error should mention param");
  }
  assert(threw && "Expected exception for missing param");

  std::cout << "PASS" << std::endl;
}

void test_invalid_regex_throws() {
  std::cout << "Test: invalid regex pattern throws... " << std::flush;

  // Clear regex cache
  clearRegexCache();

  auto batch = std::make_shared<ColumnBatch>(1);

  auto dict =
      std::make_shared<std::vector<std::string>>(std::vector<std::string>{"US"});
  auto codes = std::make_shared<std::vector<int32_t>>(std::vector<int32_t>{0});
  auto valid = std::make_shared<std::vector<uint8_t>>(std::vector<uint8_t>{1});
  auto col = std::make_shared<StringDictColumn>(dict, codes, valid);
  *batch = batch->withStringColumn(3001, col);

  PredNode pred;
  pred.op = "regex";
  pred.regex_key_id = 3001;
  pred.regex_pattern = "[invalid"; // Unclosed bracket
  pred.regex_param_id = 0;
  pred.regex_flags = "";

  ParamTable params;
  ExecCtx ctx;
  ctx.params = &params;

  bool threw = false;
  try {
    eval_pred(pred, 0, *batch, ctx);
  } catch (const std::runtime_error &e) {
    threw = true;
    std::string msg = e.what();
    assert(msg.find("regex") != std::string::npos &&
           "Error should mention regex");
  }
  assert(threw && "Expected exception for invalid regex");

  std::cout << "PASS" << std::endl;
}

int main() {
  std::cout << "=== Regex Tests ===" << std::endl;

  test_literal_pattern();
  test_param_pattern();
  test_case_insensitive();
  test_null_row_returns_false();
  test_missing_column_returns_false();
  test_missing_param_throws();
  test_invalid_regex_throws();

  std::cout << "\nAll regex tests passed!" << std::endl;
  return 0;
}
