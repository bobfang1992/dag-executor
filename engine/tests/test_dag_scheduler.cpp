#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include "async_dag_scheduler.h"
#include "async_io_clients.h"
#include "column_batch.h"
#include "cpu_pool.h"
#include "dag_scheduler.h"
#include "endpoint_registry.h"
#include "event_loop.h"
#include "executor.h"
#include "io_clients.h"
#include "param_table.h"
#include "plan.h"
#include "request.h"
#include "rowset.h"
#include "task_registry.h"
#include <chrono>
#include <optional>
#include <thread>

using namespace rankd;

// Initialize CPU pool once for all tests
static struct CPUPoolInit {
  CPUPoolInit() { InitCPUThreadPool(4); }
} cpu_pool_init;

// Helper to load endpoint registry for tests
static const EndpointRegistry &get_test_endpoint_registry() {
  static std::optional<EndpointRegistry> registry;
  if (!registry) {
    auto result =
        EndpointRegistry::LoadFromJson("artifacts/endpoints.dev.json");
    if (std::holds_alternative<EndpointRegistry>(result)) {
      registry = std::get<EndpointRegistry>(result);
    } else {
      throw std::runtime_error("Failed to load endpoint registry: " +
                               std::get<std::string>(result));
    }
  }
  return *registry;
}

// Helper to create a simple plan with sleep tasks for testing parallelism
// Creates: source -> [sleep_a, sleep_b] -> concat -> output
// With parallel execution, sleep_a and sleep_b should run concurrently
static Plan create_parallel_sleep_plan(int sleep_ms_a, int sleep_ms_b) {
  Plan plan;
  plan.schema_version = 1;
  plan.plan_name = "test_parallel_sleep";

  // Node 0: viewer source
  Node viewer_node;
  viewer_node.node_id = "source";
  viewer_node.op = "viewer";
  viewer_node.params = nlohmann::json::object();
  viewer_node.params["endpoint"] = "ep_0001";
  plan.nodes.push_back(viewer_node);

  // Node 1: sleep_a
  Node sleep_a;
  sleep_a.node_id = "sleep_a";
  sleep_a.op = "sleep";
  sleep_a.inputs = {"source"};
  sleep_a.params = nlohmann::json::object();
  sleep_a.params["duration_ms"] = sleep_ms_a;
  plan.nodes.push_back(sleep_a);

  // Node 2: sleep_b
  Node sleep_b;
  sleep_b.node_id = "sleep_b";
  sleep_b.op = "sleep";
  sleep_b.inputs = {"source"};
  sleep_b.params = nlohmann::json::object();
  sleep_b.params["duration_ms"] = sleep_ms_b;
  plan.nodes.push_back(sleep_b);

  // Node 3: concat (sleep_a + sleep_b)
  Node concat;
  concat.node_id = "concat_result";
  concat.op = "concat";
  concat.inputs = {"sleep_a"};
  concat.params = nlohmann::json::object();
  concat.params["rhs"] = "sleep_b";
  plan.nodes.push_back(concat);

  plan.outputs = {"concat_result"};
  return plan;
}

// Helper to create a linear chain plan: source -> sleep_1 -> sleep_2 -> output
// Sequential dependency: no parallelism possible
static Plan create_sequential_sleep_plan(int sleep_ms_1, int sleep_ms_2) {
  Plan plan;
  plan.schema_version = 1;
  plan.plan_name = "test_sequential_sleep";

  // Node 0: viewer source
  Node viewer_node;
  viewer_node.node_id = "source";
  viewer_node.op = "viewer";
  viewer_node.params = nlohmann::json::object();
  viewer_node.params["endpoint"] = "ep_0001";
  plan.nodes.push_back(viewer_node);

  // Node 1: sleep_1
  Node sleep_1;
  sleep_1.node_id = "sleep_1";
  sleep_1.op = "sleep";
  sleep_1.inputs = {"source"};
  sleep_1.params = nlohmann::json::object();
  sleep_1.params["duration_ms"] = sleep_ms_1;
  plan.nodes.push_back(sleep_1);

  // Node 2: sleep_2 depends on sleep_1
  Node sleep_2;
  sleep_2.node_id = "sleep_2";
  sleep_2.op = "sleep";
  sleep_2.inputs = {"sleep_1"};
  sleep_2.params = nlohmann::json::object();
  sleep_2.params["duration_ms"] = sleep_ms_2;
  plan.nodes.push_back(sleep_2);

  plan.outputs = {"sleep_2"};
  return plan;
}

TEST_CASE("parallel scheduler runs independent nodes concurrently",
          "[dag_scheduler][parallel]") {
  // Create plan with two parallel sleep nodes (50ms each)
  Plan plan = create_parallel_sleep_plan(50, 50);
  validate_plan(plan, &get_test_endpoint_registry());

  IoClients io_clients;
  ParamTable params;
  RequestContext request_ctx;
  request_ctx.user_id = 1;
  request_ctx.request_id = "test_parallel";

  ExecCtx ctx;
  ctx.params = &params;
  ctx.expr_table = &plan.expr_table;
  ctx.pred_table = &plan.pred_table;
  ctx.request = &request_ctx;
  ctx.endpoints = &get_test_endpoint_registry();
  ctx.clients = &io_clients;
  ctx.parallel = true;  // Enable parallel execution

  auto start = std::chrono::steady_clock::now();
  auto result = execute_plan(plan, ctx);
  auto end = std::chrono::steady_clock::now();

  double elapsed_ms =
      std::chrono::duration<double, std::milli>(end - start).count();

  // With parallel execution, two 50ms sleeps should complete in ~50ms
  // Sequential would take ~100ms
  // Allow some overhead (up to 80ms total)
  REQUIRE(result.outputs.size() == 1);
  REQUIRE(elapsed_ms < 80.0);
}

TEST_CASE("sequential scheduler runs nodes serially",
          "[dag_scheduler][sequential]") {
  // Create plan with two sequential sleep nodes (30ms each)
  Plan plan = create_sequential_sleep_plan(30, 30);
  validate_plan(plan, &get_test_endpoint_registry());

  IoClients io_clients;
  ParamTable params;
  RequestContext request_ctx;
  request_ctx.user_id = 1;
  request_ctx.request_id = "test_sequential";

  ExecCtx ctx;
  ctx.params = &params;
  ctx.expr_table = &plan.expr_table;
  ctx.pred_table = &plan.pred_table;
  ctx.request = &request_ctx;
  ctx.endpoints = &get_test_endpoint_registry();
  ctx.clients = &io_clients;
  ctx.parallel = false;  // Sequential execution

  auto start = std::chrono::steady_clock::now();
  auto result = execute_plan(plan, ctx);
  auto end = std::chrono::steady_clock::now();

  double elapsed_ms =
      std::chrono::duration<double, std::milli>(end - start).count();

  // Sequential execution should take at least 60ms (30+30)
  REQUIRE(result.outputs.size() == 1);
  REQUIRE(elapsed_ms >= 55.0);  // Allow small variance
}

TEST_CASE("parallel scheduler schema_deltas are deterministic",
          "[dag_scheduler][schema_deltas]") {
  // Create plan with parallel branches
  Plan plan = create_parallel_sleep_plan(10, 10);
  validate_plan(plan, &get_test_endpoint_registry());

  // Run multiple times and verify schema_deltas order is consistent
  std::vector<std::vector<std::string>> all_node_orders;

  for (int i = 0; i < 5; ++i) {
    IoClients io_clients;
    ParamTable params;
    RequestContext request_ctx;
    request_ctx.user_id = 1;
    request_ctx.request_id = "test_deterministic_" + std::to_string(i);

    ExecCtx ctx;
    ctx.params = &params;
    ctx.expr_table = &plan.expr_table;
    ctx.pred_table = &plan.pred_table;
    ctx.request = &request_ctx;
    ctx.endpoints = &get_test_endpoint_registry();
    ctx.clients = &io_clients;
    ctx.parallel = true;

    auto result = execute_plan(plan, ctx);

    std::vector<std::string> node_order;
    for (const auto &nd : result.schema_deltas) {
      node_order.push_back(nd.node_id);
    }
    all_node_orders.push_back(node_order);
  }

  // All runs should produce the same node order in schema_deltas
  for (size_t i = 1; i < all_node_orders.size(); ++i) {
    REQUIRE(all_node_orders[i] == all_node_orders[0]);
  }

  // Schema deltas should be in topo order: source, sleep_a, sleep_b,
  // concat_result
  REQUIRE(all_node_orders[0].size() == 4);
  REQUIRE(all_node_orders[0][0] == "source");
  // sleep_a and sleep_b can be in either order (both have same depth)
  // but concat_result must be last
  REQUIRE(all_node_orders[0][3] == "concat_result");
}

TEST_CASE("parallel scheduler produces same results as sequential",
          "[dag_scheduler][parity]") {
  Plan plan = create_parallel_sleep_plan(5, 5);
  validate_plan(plan, &get_test_endpoint_registry());

  // Run with sequential
  IoClients io_clients_seq;
  ParamTable params_seq;
  RequestContext request_ctx_seq;
  request_ctx_seq.user_id = 1;
  request_ctx_seq.request_id = "test_seq";

  ExecCtx ctx_seq;
  ctx_seq.params = &params_seq;
  ctx_seq.expr_table = &plan.expr_table;
  ctx_seq.pred_table = &plan.pred_table;
  ctx_seq.request = &request_ctx_seq;
  ctx_seq.endpoints = &get_test_endpoint_registry();
  ctx_seq.clients = &io_clients_seq;
  ctx_seq.parallel = false;

  auto result_seq = execute_plan(plan, ctx_seq);

  // Run with parallel
  IoClients io_clients_par;
  ParamTable params_par;
  RequestContext request_ctx_par;
  request_ctx_par.user_id = 1;
  request_ctx_par.request_id = "test_par";

  ExecCtx ctx_par;
  ctx_par.params = &params_par;
  ctx_par.expr_table = &plan.expr_table;
  ctx_par.pred_table = &plan.pred_table;
  ctx_par.request = &request_ctx_par;
  ctx_par.endpoints = &get_test_endpoint_registry();
  ctx_par.clients = &io_clients_par;
  ctx_par.parallel = true;

  auto result_par = execute_plan(plan, ctx_par);

  // Same number of outputs
  REQUIRE(result_seq.outputs.size() == result_par.outputs.size());
  REQUIRE(result_seq.outputs.size() == 1);

  // Same output size
  REQUIRE(result_seq.outputs[0].rowCount() == result_par.outputs[0].rowCount());

  // Same number of schema deltas
  REQUIRE(result_seq.schema_deltas.size() == result_par.schema_deltas.size());

  // Same node ids in schema deltas (order should match since both use topo
  // order)
  for (size_t i = 0; i < result_seq.schema_deltas.size(); ++i) {
    REQUIRE(result_seq.schema_deltas[i].node_id ==
            result_par.schema_deltas[i].node_id);
  }
}

TEST_CASE("sleep task identity behavior", "[sleep][task]") {
  auto &registry = TaskRegistry::instance();

  // Create a simple input RowSet
  auto batch_ptr = std::make_shared<ColumnBatch>(3);
  for (size_t i = 0; i < 3; ++i) {
    batch_ptr->setId(i, static_cast<int64_t>(i + 1));
  }
  RowSet input(batch_ptr);

  // Run sleep with 0ms
  nlohmann::json sleep_params;
  sleep_params["duration_ms"] = 0;
  auto vp = registry.validate_params("sleep", sleep_params);

  ExecCtx ctx;
  ParamTable params;
  ctx.params = &params;

  RowSet output = registry.execute("sleep", {input}, vp, ctx);

  // Sleep should preserve input exactly
  REQUIRE(output.rowCount() == input.rowCount());
  REQUIRE(output.logicalSize() == input.logicalSize());

  auto in_indices = input.materializeIndexViewForOutput(input.batch().size());
  auto out_indices = output.materializeIndexViewForOutput(output.batch().size());
  REQUIRE(in_indices == out_indices);
}

// =============================================================================
// Async Scheduler Tests
// =============================================================================

// Helper to create a three-branch DAG: source -> [sleep_a, sleep_b, vm] -> concat_ab -> concat_final
// Tests: multiple coroutines suspended concurrently + CPU offload (vm) doesn't block loop
static Plan create_three_branch_dag(int sleep_ms_a, int sleep_ms_b) {
  Plan plan;
  plan.schema_version = 1;
  plan.plan_name = "test_three_branch";

  // Node 0: viewer source
  Node viewer_node;
  viewer_node.node_id = "source";
  viewer_node.op = "viewer";
  viewer_node.params = nlohmann::json::object();
  viewer_node.params["endpoint"] = "ep_0001";
  plan.nodes.push_back(viewer_node);

  // Node 1: sleep_a (async timer)
  Node sleep_a;
  sleep_a.node_id = "sleep_a";
  sleep_a.op = "sleep";
  sleep_a.inputs = {"source"};
  sleep_a.params = nlohmann::json::object();
  sleep_a.params["duration_ms"] = sleep_ms_a;
  plan.nodes.push_back(sleep_a);

  // Node 2: sleep_b (async timer)
  Node sleep_b;
  sleep_b.node_id = "sleep_b";
  sleep_b.op = "sleep";
  sleep_b.inputs = {"source"};
  sleep_b.params = nlohmann::json::object();
  sleep_b.params["duration_ms"] = sleep_ms_b;
  plan.nodes.push_back(sleep_b);

  // Node 3: vm (CPU offload) - add a constant to model_score_1
  Node vm_node;
  vm_node.node_id = "vm_branch";
  vm_node.op = "vm";
  vm_node.inputs = {"source"};
  vm_node.params = nlohmann::json::object();
  vm_node.params["out_key"] = 1001;  // Key.model_score_1
  vm_node.params["expr_id"] = "vm_const";
  plan.nodes.push_back(vm_node);

  // Add expr to plan's expr_table
  auto const_expr = std::make_shared<ExprNode>();
  const_expr->op = "const_number";
  const_expr->const_value = 1.0;
  plan.expr_table["vm_const"] = const_expr;

  // Node 4: concat sleep_a + sleep_b
  Node concat_ab;
  concat_ab.node_id = "concat_ab";
  concat_ab.op = "concat";
  concat_ab.inputs = {"sleep_a"};
  concat_ab.params = nlohmann::json::object();
  concat_ab.params["rhs"] = "sleep_b";
  plan.nodes.push_back(concat_ab);

  // Node 5: concat result + vm_branch
  Node concat_final;
  concat_final.node_id = "output";
  concat_final.op = "concat";
  concat_final.inputs = {"concat_ab"};
  concat_final.params = nlohmann::json::object();
  concat_final.params["rhs"] = "vm_branch";
  plan.nodes.push_back(concat_final);

  plan.outputs = {"output"};
  return plan;
}

// Helper to create a plan with fault injection: source -> [sleep_ok, sleep_fail] -> concat
static Plan create_fault_injection_plan(int sleep_ms_ok, int sleep_ms_fail) {
  Plan plan;
  plan.schema_version = 1;
  plan.plan_name = "test_fault_injection";

  // Node 0: viewer source
  Node viewer_node;
  viewer_node.node_id = "source";
  viewer_node.op = "viewer";
  viewer_node.params = nlohmann::json::object();
  viewer_node.params["endpoint"] = "ep_0001";
  plan.nodes.push_back(viewer_node);

  // Node 1: sleep_ok (completes normally)
  Node sleep_ok;
  sleep_ok.node_id = "sleep_ok";
  sleep_ok.op = "sleep";
  sleep_ok.inputs = {"source"};
  sleep_ok.params = nlohmann::json::object();
  sleep_ok.params["duration_ms"] = sleep_ms_ok;
  sleep_ok.params["fail_after_sleep"] = false;
  plan.nodes.push_back(sleep_ok);

  // Node 2: sleep_fail (throws after sleeping)
  Node sleep_fail;
  sleep_fail.node_id = "sleep_fail";
  sleep_fail.op = "sleep";
  sleep_fail.inputs = {"source"};
  sleep_fail.params = nlohmann::json::object();
  sleep_fail.params["duration_ms"] = sleep_ms_fail;
  sleep_fail.params["fail_after_sleep"] = true;  // Fault injection!
  plan.nodes.push_back(sleep_fail);

  // Node 3: concat (won't run if either fails)
  Node concat;
  concat.node_id = "concat_result";
  concat.op = "concat";
  concat.inputs = {"sleep_ok"};
  concat.params = nlohmann::json::object();
  concat.params["rhs"] = "sleep_fail";
  plan.nodes.push_back(concat);

  plan.outputs = {"concat_result"};
  return plan;
}

TEST_CASE("async scheduler: three-branch DAG with concurrent sleep + vm",
          "[async_scheduler][concurrent]") {
  // Three branches: sleep_a (50ms), sleep_b (50ms), vm (CPU offload)
  // All run concurrently; total time should be ~50ms, not 100ms+ (sequential)
  Plan plan = create_three_branch_dag(50, 50);
  validate_plan(plan, &get_test_endpoint_registry());

  // Start event loop (internally spawns thread)
  ranking::EventLoop loop;
  loop.Start();

  ranking::AsyncIoClients async_clients;
  ParamTable params;
  RequestContext request_ctx;
  request_ctx.user_id = 1;
  request_ctx.request_id = "test_three_branch";

  auto start = std::chrono::steady_clock::now();

  auto result = ranking::execute_plan_async_blocking(
      plan, loop, async_clients, params, plan.expr_table, plan.pred_table,
      get_test_endpoint_registry(), request_ctx, nullptr);

  auto end = std::chrono::steady_clock::now();
  double elapsed_ms = std::chrono::duration<double, std::milli>(end - start).count();

  // Stop loop (joins internal thread)
  loop.Stop();

  // Verify results
  REQUIRE(result.outputs.size() == 1);

  // With concurrent execution, three 50ms branches should complete in ~50-80ms
  // Sequential would take 150ms+
  INFO("Elapsed time: " << elapsed_ms << "ms");
  REQUIRE(elapsed_ms < 120.0);  // Allow overhead but verify parallelism

  // Verify output has expected rows (source + source + source from 3 branches)
  // Source returns 1 row (viewer), concat doubles it each time
  REQUIRE(result.outputs[0].rowCount() >= 1);
}

TEST_CASE("async scheduler: fault injection - no deadlock or UAF on error",
          "[async_scheduler][fault_injection]") {
  // Two parallel branches: one succeeds (100ms), one fails after 20ms
  // The failing branch should trigger error without deadlock
  // And the successful branch should complete without UAF
  Plan plan = create_fault_injection_plan(100, 20);
  validate_plan(plan, &get_test_endpoint_registry());

  // Start event loop (internally spawns thread)
  ranking::EventLoop loop;
  loop.Start();

  ranking::AsyncIoClients async_clients;
  ParamTable params;
  RequestContext request_ctx;
  request_ctx.user_id = 1;
  request_ctx.request_id = "test_fault_injection";

  auto start = std::chrono::steady_clock::now();

  bool caught_error = false;
  std::string error_message;

  try {
    ranking::execute_plan_async_blocking(plan, loop, async_clients, params,
                                          plan.expr_table, plan.pred_table,
                                          get_test_endpoint_registry(), request_ctx,
                                          nullptr);
  } catch (const std::exception& e) {
    caught_error = true;
    error_message = e.what();
  }

  auto end = std::chrono::steady_clock::now();
  double elapsed_ms = std::chrono::duration<double, std::milli>(end - start).count();

  // Stop loop (joins internal thread; should not hang - no deadlock)
  loop.Stop();

  // Verify error was thrown
  REQUIRE(caught_error);
  REQUIRE_THAT(error_message,
               Catch::Matchers::ContainsSubstring("intentional failure"));

  // Verify timing: should complete after ~100ms (waiting for all inflight)
  // not immediately after 20ms (would indicate premature return)
  INFO("Elapsed time: " << elapsed_ms << "ms");
  REQUIRE(elapsed_ms >= 90.0);   // Wait for 100ms branch to complete
  REQUIRE(elapsed_ms < 200.0);   // But not hung/deadlocked

  // If we get here without crash/hang, no UAF or deadlock occurred!
}
