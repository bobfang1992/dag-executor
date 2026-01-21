#include <CLI/CLI.hpp>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <nlohmann/json.hpp>
#include <random>
#include <sstream>
#include <string>

#include "capability_registry.h"
#include "capability_registry_gen.h"
#include "endpoint_registry.h"
#include "executor.h"
#include "feature_registry.h"
#include "io_clients.h"
#include "key_registry.h"
#include "param_registry.h"
#include "param_table.h"
#include "plan.h"
#include "pred_eval.h"
#include "request.h"
#include "task_registry.h"
#include "validation.h"

using json = nlohmann::ordered_json; // ordered for deterministic output

// Use generated validation from registry/validation.toml
using rankd::validation::is_valid_plan_name;

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
  std::string plan_dir = "artifacts/plans";
  std::string plan_name;
  std::string artifacts_dir = "artifacts";
  std::string env = "dev";
  bool print_registry = false;
  bool print_task_manifest = false;
  bool list_plans = false;
  bool print_plan_info = false;
  bool dump_run_trace = false;

  app.add_option("--plan", plan_path, "Path to plan JSON file");
  app.add_option("--plan_dir", plan_dir,
                 "Plan store directory (default: artifacts/plans)");
  app.add_option("--plan_name", plan_name,
                 "Plan name to load from plan_dir (resolves to "
                 "<plan_dir>/<name>.plan.json)");
  app.add_flag("--print-registry", print_registry,
               "Print registry digests and exit");
  app.add_flag("--print-task-manifest", print_task_manifest,
               "Print task manifest TOML and exit");
  app.add_flag("--list-plans", list_plans,
               "List available plans from plan_dir/index.json and exit");
  app.add_flag("--print-plan-info", print_plan_info,
               "Print plan info (including capabilities_digest) and exit");
  app.add_flag("--dump-run-trace", dump_run_trace,
               "Include runtime trace (schema_deltas) in response");
  app.add_option("--artifacts_dir", artifacts_dir,
                 "Artifacts directory (default: artifacts)");
  app.add_option("--env", env, "Environment: dev, test, or prod (default: dev)")
      ->check(CLI::IsMember({"dev", "test", "prod"}));

  CLI11_PARSE(app, argc, argv);

  // Load endpoint registry
  std::string endpoints_path = artifacts_dir + "/endpoints." + env + ".json";
  auto endpoints_result = rankd::EndpointRegistry::LoadFromJson(endpoints_path);
  if (std::holds_alternative<std::string>(endpoints_result)) {
    // Only fail if we actually need endpoints (not for print-registry without endpoints)
    // For now, we'll allow print-registry to work without endpoints loaded
    if (!print_registry && !print_task_manifest && !list_plans) {
      std::cerr << "Warning: Failed to load endpoint registry: "
                << std::get<std::string>(endpoints_result) << std::endl;
    }
  }
  rankd::EndpointRegistry* endpoint_registry = nullptr;
  if (std::holds_alternative<rankd::EndpointRegistry>(endpoints_result)) {
    endpoint_registry = &std::get<rankd::EndpointRegistry>(endpoints_result);
  }

  // Handle --print-registry
  if (print_registry) {
    const auto &task_registry = rankd::TaskRegistry::instance();
    json output;
    output["key_registry_digest"] = std::string(rankd::kKeyRegistryDigest);
    output["param_registry_digest"] = std::string(rankd::kParamRegistryDigest);
    output["feature_registry_digest"] =
        std::string(rankd::kFeatureRegistryDigest);
    output["capability_registry_digest"] =
        std::string(rankd::kCapabilityRegistryDigest);
    output["task_manifest_digest"] = task_registry.compute_manifest_digest();
    output["num_keys"] = rankd::kKeyCount;
    output["num_params"] = rankd::kParamCount;
    output["num_features"] = rankd::kFeatureCount;
    output["num_capabilities"] = rankd::kCapabilityCount;
    output["num_tasks"] = task_registry.num_tasks();

    // Add endpoint registry info if loaded
    if (endpoint_registry) {
      output["endpoint_registry_digest"] = endpoint_registry->registry_digest();
      output["endpoints_config_digest"] = endpoint_registry->config_digest();
      output["endpoints_env"] = endpoint_registry->env();
      output["num_endpoints"] = endpoint_registry->size();
    } else {
      output["endpoint_registry_digest"] = nullptr;
      output["endpoints_config_digest"] = nullptr;
      output["endpoints_env"] = env;
      output["num_endpoints"] = 0;
    }

    std::cout << output.dump() << std::endl;
    return 0;
  }

  // Handle --print-task-manifest
  if (print_task_manifest) {
    const auto &task_registry = rankd::TaskRegistry::instance();
    std::cout << task_registry.to_toml() << std::endl;
    return 0;
  }

  // Handle --list-plans
  if (list_plans) {
    std::string index_path = plan_dir + "/index.json";
    std::ifstream index_file(index_path);
    if (!index_file.is_open()) {
      std::cerr << "Error: Cannot open " << index_path << std::endl;
      return 1;
    }
    try {
      json index = json::parse(index_file);
      if (index.contains("plans") && index["plans"].is_array()) {
        std::cout << "Available plans in " << plan_dir << ":" << std::endl;
        for (const auto &plan : index["plans"]) {
          if (plan.contains("name") && plan["name"].is_string()) {
            std::cout << "  " << plan["name"].get<std::string>() << std::endl;
          }
        }
      }
    } catch (const json::parse_error &e) {
      std::cerr << "Error: Failed to parse " << index_path << ": " << e.what()
                << std::endl;
      return 1;
    }
    return 0;
  }

  // Handle --print-plan-info (requires --plan or --plan_name)
  if (print_plan_info) {
    // Resolve plan_name to plan_path if needed
    if (!plan_name.empty()) {
      if (!is_valid_plan_name(plan_name)) {
        std::cerr << "Error: Invalid plan_name '" << plan_name
                  << "'. Plan names must match [A-Za-z0-9_]+ only." << std::endl;
        return 1;
      }
      plan_path = plan_dir + "/" + plan_name + ".plan.json";
    }

    if (plan_path.empty()) {
      std::cerr << "Error: --print-plan-info requires --plan or --plan_name"
                << std::endl;
      return 1;
    }

    try {
      rankd::Plan plan = rankd::parse_plan(plan_path);

      json output;
      output["plan_name"] = plan.plan_name;
      output["capabilities_required"] = plan.capabilities_required;
      output["extensions"] = plan.extensions.is_null()
                                 ? nlohmann::json::object()
                                 : plan.extensions;
      output["capabilities_digest"] = rankd::compute_capabilities_digest(
          plan.capabilities_required, plan.extensions);

      // Check for unsupported capabilities first (fail-closed)
      std::vector<std::string> unsupported_caps;
      for (const auto &cap : plan.capabilities_required) {
        if (!rankd::capability_is_supported(cap)) {
          unsupported_caps.push_back(cap);
        }
      }

      if (!unsupported_caps.empty()) {
        // Output structured error and exit non-zero
        output["error"] = {{"code", "UNSUPPORTED_CAPABILITY"},
                           {"unsupported", unsupported_caps}};
        std::cout << output.dump() << std::endl;
        return 1;
      }

      // Validate plan to populate writes_eval fields (RFC0005)
      rankd::validate_plan(plan, endpoint_registry);

      // Add nodes with writes_eval
      json nodes_arr = json::array();
      for (const auto &node : plan.nodes) {
        json node_json;
        node_json["node_id"] = node.node_id;
        node_json["op"] = node.op;
        node_json["writes_eval"] = {
            {"kind", rankd::effect_kind_to_string(node.writes_eval_kind)},
            {"keys", node.writes_eval_keys}};
        nodes_arr.push_back(node_json);
      }
      output["nodes"] = nodes_arr;

      std::cout << output.dump() << std::endl;
      return 0;
    } catch (const std::exception &e) {
      std::cerr << "Error: " << e.what() << std::endl;
      return 1;
    }
  }

  // Resolve plan_name to plan_path
  if (!plan_name.empty()) {
    if (!is_valid_plan_name(plan_name)) {
      std::cerr << "Error: Invalid plan_name '" << plan_name
                << "'. Plan names must match [A-Za-z0-9_]+ only." << std::endl;
      return 1;
    }
    if (!plan_path.empty()) {
      std::cerr << "Error: Cannot specify both --plan and --plan_name"
                << std::endl;
      return 1;
    }
    plan_path = plan_dir + "/" + plan_name + ".plan.json";
  }

  // Read all stdin
  std::ostringstream input_buf;
  input_buf << std::cin.rdbuf();
  std::string input = input_buf.str();

  // Parse request JSON
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

  // Parse and validate request context (request_id, user_id)
  auto parse_result = rankd::parse_request_context(request);
  if (!parse_result.ok) {
    json error_response;
    error_response["error"] = "Invalid request";
    error_response["detail"] = parse_result.error;
    std::cout << error_response.dump() << std::endl;
    return 1;
  }
  rankd::RequestContext request_context = std::move(parse_result.context);

  // Build response
  json response;
  response["request_id"] = request_context.request_id;

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
  // IoClients owns per-request client cache (Redis, etc.)
  rankd::IoClients io_clients;

  rankd::ExecCtx ctx;
  ctx.params = &param_table;
  ctx.request = &request_context;
  ctx.endpoints = endpoint_registry;
  ctx.clients = &io_clients;

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
      rankd::validate_plan(plan, endpoint_registry);

      // Set expr_table and pred_table in context
      ctx.expr_table = &plan.expr_table;
      ctx.pred_table = &plan.pred_table;

      auto exec_result = rankd::execute_plan(plan, ctx);

      // Merge all outputs into candidates
      for (const auto &rowset : exec_result.outputs) {
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

      // Include schema_deltas if --dump-run-trace is set
      if (dump_run_trace) {
        json schema_deltas = json::array();
        for (const auto &nd : exec_result.schema_deltas) {
          json delta_json;
          delta_json["node_id"] = nd.node_id;
          delta_json["in_keys_union"] = nd.delta.in_keys_union;
          delta_json["out_keys"] = nd.delta.out_keys;
          delta_json["new_keys"] = nd.delta.new_keys;
          delta_json["removed_keys"] = nd.delta.removed_keys;
          schema_deltas.push_back(delta_json);
        }
        response["schema_deltas"] = schema_deltas;
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
