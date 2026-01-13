#include <iostream>
#include <sstream>
#include <random>
#include <iomanip>
#include <string>
#include <nlohmann/json.hpp>
#include <CLI/CLI.hpp>

#include "plan.h"
#include "executor.h"
#include "task_registry.h"
#include "keys.h"
#include "params.h"
#include "features.h"

using json = nlohmann::ordered_json;  // ordered for deterministic output

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

int main(int argc, char* argv[]) {
    CLI::App app{"rankd - Ranking DAG executor"};

    std::string plan_path;
    bool print_registry = false;

    app.add_option("--plan", plan_path, "Path to plan JSON file");
    app.add_flag("--print-registry", print_registry, "Print registry digests and exit");

    CLI11_PARSE(app, argc, argv);

    // Handle --print-registry
    if (print_registry) {
        json output;
        output["key_registry_digest"] = std::string(rankd::kKeyRegistryDigest);
        output["param_registry_digest"] = std::string(rankd::kParamRegistryDigest);
        output["feature_registry_digest"] = std::string(rankd::kFeatureRegistryDigest);
        output["num_keys"] = rankd::kKeyCount;
        output["num_params"] = rankd::kParamCount;
        output["num_features"] = rankd::kFeatureCount;
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
    } catch (const json::parse_error& e) {
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
            rankd::Plan plan = rankd::parse_plan(plan_path);
            rankd::validate_plan(plan);
            auto outputs = rankd::execute_plan(plan);

            // Merge all outputs into candidates
            for (const auto& rowset : outputs) {
                auto indices = rowset.materializeIndexViewForOutput(rowset.batch->size());
                for (uint32_t idx : indices) {
                    json candidate;
                    candidate["id"] = rowset.batch->getId(idx);
                    candidate["fields"] = json::object();
                    candidates.push_back(candidate);
                }
            }
        } catch (const std::exception& e) {
            std::cerr << "Error: " << e.what() << std::endl;
            return 1;
        }
    }

    response["candidates"] = candidates;

    std::cout << response.dump() << std::endl;
    return 0;
}
