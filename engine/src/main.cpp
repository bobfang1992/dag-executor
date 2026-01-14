#include <CLI/CLI.hpp>
#include <iomanip>
#include <iostream>
#include <nlohmann/json.hpp>
#include <random>
#include <sstream>
#include <string>

#include "executor.h"
#include "feature_registry.h"
#include "key_registry.h"
#include "param_registry.h"
#include "param_table.h"
#include "plan.h"
#include "pred_eval.h"
#include "task_registry.h"

using json = nlohmann::ordered_json; // ordered for deterministic output

std::string generate_uuid() {
  static std::random_device rd;
  static std::mt19937_64 gen(rd());
  static std::uniform_int_distribution<uint64_t> dis;

  uint64_t a = dis(gen);
  uint64_t b = dis(gen);

  std::ostringstream ss;
  ss << std::hex << std::setfill('0');
  ss << std::setw(8) << ((a >> 32) & 0xFFFFFFFF) << "-";
  ss << std::setw(4) << ((a >> 16) & 0xFFFF) << "-";
  ss << std::setw(4) << ((a) & 0xFFFF) << "-";
  ss << std::setw(4) << ((b >> 48) & 0xFFFF) << "-";
  ss << std::setw(12) << (b & 0xFFFFFFFFFFFF);
  return ss.str();
}

int main(int argc, char *argv[]) {
  CLI::App app{"rankd - Ranking DAG executor"};

  std::string plan_path;
  bool print_registry = false;

  app.add_option("--plan", plan_path, "Path to plan JSON file");
  app.add_flag("--print-registry", print_registry,
               "Print registry digests and exit");

  CLI11_PARSE(app, argc, argv);

  // Handle --print-registry
  if (print_registry) {
    const auto &task_registry = rankd::TaskRegistry::instance();
    json output;
    output["key_registry_digest"] = std::string(rankd::kKeyRegistryDigest);
    output["param_registry_digest"] = std::string(rankd::kParamRegistryDigest);
    output["feature_registry_digest"] =
        std::string(rankd::kFeatureRegistryDigest);
    output["task_manifest_digest"] = task_registry.compute_manifest_digest();
    output["num_keys"] = rankd::kKeyCount;
    output["num_params"] = rankd::kParamCount;
    output["num_features"] = rankd::kFeatureCount;
    output["num_tasks"] = task_registry.num_tasks();
    std::cout << output.dump() << std::endl;
    return 0;
  }

  // Read all stdin
  std::ostringstream input_buf;
  input_buf << std::cin.rdbuf();
  std::string input = input_buf.str();

  // Parse request
  json request;
  try {
    request = json::parse(input);
  } catch (const json::parse_error &e) {
    json error_response;
    error_response["error"] = "Invalid JSON input";
    error_response["detail"] = e.what();
    std::cout << error_response.dump() << std::endl;
    return 1;
  }

  // Build response
  json response;

  // request_id: echo if provided, generate otherwise
  if (request.contains("request_id") && request["request_id"].is_string()) {
    response["request_id"] = request["request_id"];
  } else {
    response["request_id"] = generate_uuid();
  }

  // engine_request_id: always generated
  response["engine_request_id"] = generate_uuid();

  // Parse and validate param_overrides
  rankd::ParamTable param_table;
  if (request.contains("param_overrides")) {
    const auto &overrides = request["param_overrides"];
    if (!overrides.is_null()) {
      try {
        param_table = rankd::ParamTable::fromParamOverrides(overrides);
      } catch (const std::runtime_error &e) {
        json error_response;
        error_response["error"] = "Invalid param_overrides";
        error_response["detail"] = e.what();
        std::cout << error_response.dump() << std::endl;
        return 1;
      }
    }
  }

  // Build execution context
  rankd::ExecCtx ctx;
  ctx.params = &param_table;

  // Generate candidates
  json candidates = json::array();

  if (plan_path.empty()) {
    // Step 00 fallback: 5 synthetic candidates
    for (int i = 1; i <= 5; ++i) {
      json candidate;
      candidate["id"] = i;
      candidate["fields"] = json::object();
      candidates.push_back(candidate);
    }
  } else {
    // Load and execute plan
    try {
      // Clear regex cache to avoid stale pointer-based lookups across requests
      rankd::clearRegexCache();

      rankd::Plan plan = rankd::parse_plan(plan_path);
      rankd::validate_plan(plan);

      // Set expr_table and pred_table in context
      ctx.expr_table = &plan.expr_table;
      ctx.pred_table = &plan.pred_table;

      auto outputs = rankd::execute_plan(plan, ctx);

      // Merge all outputs into candidates
      for (const auto &rowset : outputs) {
        auto indices =
            rowset.materializeIndexViewForOutput(rowset.batch().size());

        // Get float column key_ids in ascending order for deterministic output
        auto float_key_ids = rowset.batch().getFloatKeyIds();

        // Get string column key_ids in ascending order for deterministic output
        auto string_key_ids = rowset.batch().getStringKeyIds();

        for (uint32_t idx : indices) {
          json candidate;
          candidate["id"] = rowset.batch().getId(idx);

          // Build fields from float columns
          json fields = json::object();
          for (uint32_t key_id : float_key_ids) {
            const auto *col = rowset.batch().getFloatCol(key_id);
            if (col && col->valid[idx]) {
              // Look up key name
              for (const auto &meta : rankd::kKeyRegistry) {
                if (meta.id == key_id) {
                  fields[std::string(meta.name)] = col->values[idx];
                  break;
                }
              }
            }
          }

          // Build fields from string columns
          for (uint32_t key_id : string_key_ids) {
            const auto *col = rowset.batch().getStringCol(key_id);
            if (col && (*col->valid)[idx]) {
              // Look up key name
              for (const auto &meta : rankd::kKeyRegistry) {
                if (meta.id == key_id) {
                  int32_t code = (*col->codes)[idx];
                  fields[std::string(meta.name)] = (*col->dict)[code];
                  break;
                }
              }
            }
          }

          candidate["fields"] = fields;

          candidates.push_back(candidate);
        }
      }
    } catch (const std::exception &e) {
      std::cerr << "Error: " << e.what() << std::endl;
      return 1;
    }
  }

  response["candidates"] = candidates;

  std::cout << response.dump() << std::endl;
  return 0;
}
