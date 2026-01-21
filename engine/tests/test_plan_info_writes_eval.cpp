#include <catch2/catch_test_macros.hpp>

#include "endpoint_registry.h"
#include "executor.h"
#include "key_registry.h"
#include "plan.h"
#include "writes_effect.h"
#include <algorithm>

using namespace rankd;

// Helper to load endpoint registry for tests
static const EndpointRegistry& get_test_endpoint_registry() {
  static std::optional<EndpointRegistry> registry;
  if (!registry) {
    auto result = EndpointRegistry::LoadFromJson("artifacts/endpoints.dev.json");
    if (std::holds_alternative<EndpointRegistry>(result)) {
      registry = std::get<EndpointRegistry>(result);
    } else {
      throw std::runtime_error("Failed to load endpoint registry: " + std::get<std::string>(result));
    }
  }
  return *registry;
}

// Helper to find a node by op in the parsed plan
static const Node* find_node_by_op(const Plan& plan, const std::string& op) {
  for (const auto& node : plan.nodes) {
    if (node.op == op) {
      return &node;
    }
  }
  return nullptr;
}

// Helper to check keys are sorted and unique
static bool is_sorted_unique(const std::vector<uint32_t>& keys) {
  if (keys.empty()) return true;
  for (size_t i = 1; i < keys.size(); ++i) {
    if (keys[i] <= keys[i - 1]) return false;
  }
  return true;
}

TEST_CASE("Fixture A: vm + row-only ops writes_eval", "[writes_eval][plan_info]") {
  // Load the vm_and_row_ops fixture
  Plan plan = parse_plan("engine/tests/fixtures/plan_info/vm_and_row_ops.plan.json");

  // Validate to populate writes_eval fields
  validate_plan(plan, &get_test_endpoint_registry());

  SECTION("vm node has Exact with out_key") {
    const Node* vm = find_node_by_op(plan, "vm");
    REQUIRE(vm != nullptr);

    // vm node should have Exact kind
    REQUIRE(vm->writes_eval_kind == EffectKind::Exact);

    // vm node should have exactly the out_key (2001 = final_score)
    REQUIRE(vm->writes_eval_keys.size() == 1);
    REQUIRE(vm->writes_eval_keys[0] == static_cast<uint32_t>(KeyId::final_score));

    // Keys should be sorted and unique
    REQUIRE(is_sorted_unique(vm->writes_eval_keys));
  }

  SECTION("filter node has Exact with empty keys") {
    const Node* filter = find_node_by_op(plan, "filter");
    REQUIRE(filter != nullptr);

    REQUIRE(filter->writes_eval_kind == EffectKind::Exact);
    REQUIRE(filter->writes_eval_keys.empty());
  }

  SECTION("take node has Exact with empty keys") {
    const Node* take = find_node_by_op(plan, "take");
    REQUIRE(take != nullptr);

    REQUIRE(take->writes_eval_kind == EffectKind::Exact);
    REQUIRE(take->writes_eval_keys.empty());
  }

  SECTION("follow source has Exact with country") {
    const Node* source = find_node_by_op(plan, "follow");
    REQUIRE(source != nullptr);

    REQUIRE(source->writes_eval_kind == EffectKind::Exact);
    // follow task writes country (hydrated from user:{id})
    REQUIRE(source->writes_eval_keys.size() == 1);
    REQUIRE(source->writes_eval_keys[0] == static_cast<uint32_t>(KeyId::country));
  }
}

TEST_CASE("Fixture B: fixed-writes source writes_eval", "[writes_eval][plan_info]") {
  // Load the fixed_source fixture
  Plan plan = parse_plan("engine/tests/fixtures/plan_info/fixed_source.plan.json");

  // Validate to populate writes_eval fields
  validate_plan(plan, &get_test_endpoint_registry());

  SECTION("recommendation has Exact with country") {
    const Node* cached = find_node_by_op(plan, "recommendation");
    REQUIRE(cached != nullptr);

    REQUIRE(cached->writes_eval_kind == EffectKind::Exact);
    // recommendation task writes country (hydrated from user:{id})
    REQUIRE(cached->writes_eval_keys.size() == 1);
    REQUIRE(cached->writes_eval_keys[0] == static_cast<uint32_t>(KeyId::country));
  }

  SECTION("concat node has Exact with empty keys") {
    const Node* concat = find_node_by_op(plan, "concat");
    REQUIRE(concat != nullptr);

    REQUIRE(concat->writes_eval_kind == EffectKind::Exact);
    REQUIRE(concat->writes_eval_keys.empty());
  }

  SECTION("take node has Exact with empty keys") {
    const Node* take = find_node_by_op(plan, "take");
    REQUIRE(take != nullptr);

    REQUIRE(take->writes_eval_kind == EffectKind::Exact);
    REQUIRE(take->writes_eval_keys.empty());
  }

  SECTION("follow source has Exact with country") {
    const Node* source = find_node_by_op(plan, "follow");
    REQUIRE(source != nullptr);

    REQUIRE(source->writes_eval_kind == EffectKind::Exact);
    // follow task writes country (hydrated from user:{id})
    REQUIRE(source->writes_eval_keys.size() == 1);
    REQUIRE(source->writes_eval_keys[0] == static_cast<uint32_t>(KeyId::country));
  }
}

TEST_CASE("writes_eval keys are always sorted and unique", "[writes_eval][plan_info]") {
  // Test both fixtures to ensure keys are always sorted and unique

  SECTION("vm_and_row_ops fixture") {
    Plan plan = parse_plan("engine/tests/fixtures/plan_info/vm_and_row_ops.plan.json");
    validate_plan(plan, &get_test_endpoint_registry());

    for (const auto& node : plan.nodes) {
      INFO("Checking node: " << node.node_id << " (" << node.op << ")");
      REQUIRE(is_sorted_unique(node.writes_eval_keys));
    }
  }

  SECTION("fixed_source fixture") {
    Plan plan = parse_plan("engine/tests/fixtures/plan_info/fixed_source.plan.json");
    validate_plan(plan, &get_test_endpoint_registry());

    for (const auto& node : plan.nodes) {
      INFO("Checking node: " << node.node_id << " (" << node.op << ")");
      REQUIRE(is_sorted_unique(node.writes_eval_keys));
    }
  }
}
