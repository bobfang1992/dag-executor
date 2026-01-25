cmake_minimum_required(VERSION 3.20)
# Allow older cmake_minimum_required in FetchContent dependencies (hiredis uses old cmake)
set(CMAKE_POLICY_VERSION_MINIMUM 3.5 CACHE STRING "")
project(rankd LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Fetch dependencies
include(FetchContent)

FetchContent_Declare(
  json
  GIT_REPOSITORY https://github.com/nlohmann/json.git
  GIT_TAG v3.11.3
)

FetchContent_Declare(
  cli11
  GIT_REPOSITORY https://github.com/CLIUtils/CLI11.git
  GIT_TAG v2.4.2
)

FetchContent_Declare(
  Catch2
  GIT_REPOSITORY https://github.com/catchorg/Catch2.git
  GIT_TAG v3.5.2
)

FetchContent_Declare(
  re2
  GIT_REPOSITORY https://github.com/google/re2.git
  GIT_TAG 2022-04-01
)

FetchContent_Declare(
  hiredis
  GIT_REPOSITORY https://github.com/redis/hiredis.git
  GIT_TAG v1.2.0
)

FetchContent_Declare(
  libuv
  GIT_REPOSITORY https://github.com/libuv/libuv.git
  GIT_TAG v1.48.0
)

# Disable Catch2's own tests
set(CATCH_BUILD_TESTING OFF CACHE BOOL "" FORCE)

# Disable RE2 tests
set(RE2_BUILD_TESTING OFF CACHE BOOL "" FORCE)

# Disable hiredis tests and SSL
set(DISABLE_TESTS ON CACHE BOOL "" FORCE)
set(ENABLE_SSL OFF CACHE BOOL "" FORCE)

# Disable libuv tests
set(LIBUV_BUILD_TESTS OFF CACHE BOOL "" FORCE)

FetchContent_MakeAvailable(json cli11 Catch2 re2 hiredis libuv)

# =============================================================================
# Task registration with namespace inference
# =============================================================================
#
# Tasks live in namespace folders: src/tasks/<namespace>/<task>.cpp
# Namespace is inferred from folder name and injected via TASK_NAMESPACE define.
#
# Example: src/tasks/core/vm.cpp -> TASK_NAMESPACE="core"
#          src/tasks/test/sleep.cpp -> TASK_NAMESPACE="test"
#

# Helper function: register a task source file with namespace compile definition
function(register_task target source_file)
  # Extract namespace from path: src/tasks/<namespace>/<file>.cpp
  get_filename_component(parent_dir "${source_file}" DIRECTORY)
  get_filename_component(namespace "${parent_dir}" NAME)

  # Set compile definition for this source file only
  set_source_files_properties("${source_file}"
    TARGET_DIRECTORY ${target}
    PROPERTIES COMPILE_DEFINITIONS "TASK_NAMESPACE=\"${namespace}\""
  )
endfunction()

# Task source files organized by namespace
set(TASK_SOURCES
  # core:: namespace - shared production tasks
  src/tasks/core/viewer.cpp
  src/tasks/core/follow.cpp
  src/tasks/core/media.cpp
  src/tasks/core/recommendation.cpp
  src/tasks/core/take.cpp
  src/tasks/core/sort.cpp
  src/tasks/core/vm.cpp
  src/tasks/core/filter.cpp
  src/tasks/core/concat.cpp
  # test:: namespace - testing/debugging tasks
  src/tasks/test/sleep.cpp
  src/tasks/test/fixed_source.cpp
  src/tasks/test/busy_cpu.cpp
)

add_executable(rankd
  src/main.cpp
  src/plan.cpp
  src/executor.cpp
  src/dag_scheduler.cpp
  src/async_dag_scheduler.cpp
  src/cpu_pool.cpp
  src/task_registry.cpp
  src/output_contract.cpp
  src/capability_registry.cpp
  src/writes_effect.cpp
  src/redis_client.cpp
  src/io_clients.cpp
  src/thread_pool.cpp
  src/inflight_limiter.cpp
  src/event_loop.cpp
  src/async_redis_client.cpp
  src/async_io_clients.cpp
  src/bench_event_loop.cpp
  ${TASK_SOURCES}
)

target_include_directories(rankd PRIVATE
  ${CMAKE_SOURCE_DIR}/include
  ${hiredis_SOURCE_DIR}  # For adapters/libuv.h
)
target_link_libraries(rankd PRIVATE nlohmann_json::nlohmann_json CLI11::CLI11 re2::re2 hiredis::hiredis uv_a)

# Register task namespaces for rankd
foreach(task_source ${TASK_SOURCES})
  register_task(rankd "${task_source}")
endforeach()

# Output to engine/bin/
set_target_properties(rankd PROPERTIES
  RUNTIME_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}/bin"
)

# Test executable (Catch2)
add_executable(rankd_tests
  tests/test_rowset.cpp
  tests/test_param_table.cpp
  tests/test_pred_eval.cpp
  tests/test_sort.cpp
  tests/test_request.cpp
  tests/test_endpoint_registry.cpp
  tests/test_inflight_limiter.cpp
  src/task_registry.cpp
  src/output_contract.cpp
  src/writes_effect.cpp
  src/redis_client.cpp
  src/io_clients.cpp
  src/thread_pool.cpp
  src/inflight_limiter.cpp
  src/event_loop.cpp
  src/async_redis_client.cpp
  src/async_io_clients.cpp
  ${TASK_SOURCES}
)

target_include_directories(rankd_tests PRIVATE
  ${CMAKE_SOURCE_DIR}/include
  ${hiredis_SOURCE_DIR}
)
target_link_libraries(rankd_tests PRIVATE nlohmann_json::nlohmann_json Catch2::Catch2WithMain re2::re2 hiredis::hiredis uv_a)

# Register task namespaces for rankd_tests
foreach(task_source ${TASK_SOURCES})
  register_task(rankd_tests "${task_source}")
endforeach()

set_target_properties(rankd_tests PROPERTIES
  RUNTIME_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}/bin"
)

# Concat tests executable (Catch2)
add_executable(concat_tests
  tests/test_concat.cpp
  src/plan.cpp
  src/executor.cpp
  src/dag_scheduler.cpp
  src/cpu_pool.cpp
  src/task_registry.cpp
  src/output_contract.cpp
  src/capability_registry.cpp
  src/writes_effect.cpp
  src/redis_client.cpp
  src/io_clients.cpp
  src/thread_pool.cpp
  src/inflight_limiter.cpp
  src/event_loop.cpp
  src/async_redis_client.cpp
  src/async_io_clients.cpp
  ${TASK_SOURCES}
)

target_include_directories(concat_tests PRIVATE
  ${CMAKE_SOURCE_DIR}/include
  ${hiredis_SOURCE_DIR}
)
target_link_libraries(concat_tests PRIVATE nlohmann_json::nlohmann_json Catch2::Catch2WithMain re2::re2 hiredis::hiredis uv_a)

# Register task namespaces for concat_tests
foreach(task_source ${TASK_SOURCES})
  register_task(concat_tests "${task_source}")
endforeach()

set_target_properties(concat_tests PROPERTIES
  RUNTIME_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}/bin"
)

# Regex tests executable (simple main, no Catch2)
add_executable(regex_tests
  tests/test_regex.cpp
  src/plan.cpp
  src/executor.cpp
  src/dag_scheduler.cpp
  src/cpu_pool.cpp
  src/task_registry.cpp
  src/output_contract.cpp
  src/capability_registry.cpp
  src/writes_effect.cpp
  src/redis_client.cpp
  src/io_clients.cpp
  src/thread_pool.cpp
  src/inflight_limiter.cpp
  src/event_loop.cpp
  src/async_redis_client.cpp
  src/async_io_clients.cpp
  ${TASK_SOURCES}
)

target_include_directories(regex_tests PRIVATE
  ${CMAKE_SOURCE_DIR}/include
  ${hiredis_SOURCE_DIR}
)
target_link_libraries(regex_tests PRIVATE nlohmann_json::nlohmann_json re2::re2 hiredis::hiredis uv_a)

# Register task namespaces for regex_tests
foreach(task_source ${TASK_SOURCES})
  register_task(regex_tests "${task_source}")
endforeach()

set_target_properties(regex_tests PROPERTIES
  RUNTIME_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}/bin"
)

# WritesEffect tests executable (Catch2)
add_executable(writes_effect_tests
  tests/test_writes_effect.cpp
  src/writes_effect.cpp
)

target_include_directories(writes_effect_tests PRIVATE ${CMAKE_SOURCE_DIR}/include)
target_link_libraries(writes_effect_tests PRIVATE nlohmann_json::nlohmann_json Catch2::Catch2WithMain)

set_target_properties(writes_effect_tests PROPERTIES
  RUNTIME_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}/bin"
)

# Plan info writes_eval tests executable (Catch2)
add_executable(plan_info_tests
  tests/test_plan_info_writes_eval.cpp
  src/plan.cpp
  src/executor.cpp
  src/dag_scheduler.cpp
  src/cpu_pool.cpp
  src/task_registry.cpp
  src/output_contract.cpp
  src/capability_registry.cpp
  src/writes_effect.cpp
  src/redis_client.cpp
  src/io_clients.cpp
  src/thread_pool.cpp
  src/inflight_limiter.cpp
  src/event_loop.cpp
  src/async_redis_client.cpp
  src/async_io_clients.cpp
  ${TASK_SOURCES}
)

target_include_directories(plan_info_tests PRIVATE
  ${CMAKE_SOURCE_DIR}/include
  ${hiredis_SOURCE_DIR}
)
target_link_libraries(plan_info_tests PRIVATE nlohmann_json::nlohmann_json Catch2::Catch2WithMain re2::re2 hiredis::hiredis uv_a)

# Register task namespaces for plan_info_tests
foreach(task_source ${TASK_SOURCES})
  register_task(plan_info_tests "${task_source}")
endforeach()

set_target_properties(plan_info_tests PROPERTIES
  RUNTIME_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}/bin"
)

# Runtime schema delta tests executable (Catch2)
add_executable(schema_delta_tests
  tests/test_runtime_schema_delta.cpp
  src/plan.cpp
  src/executor.cpp
  src/dag_scheduler.cpp
  src/cpu_pool.cpp
  src/task_registry.cpp
  src/output_contract.cpp
  src/capability_registry.cpp
  src/writes_effect.cpp
  src/redis_client.cpp
  src/io_clients.cpp
  src/thread_pool.cpp
  src/inflight_limiter.cpp
  src/event_loop.cpp
  src/async_redis_client.cpp
  src/async_io_clients.cpp
  ${TASK_SOURCES}
)

target_include_directories(schema_delta_tests PRIVATE
  ${CMAKE_SOURCE_DIR}/include
  ${hiredis_SOURCE_DIR}
)
target_link_libraries(schema_delta_tests PRIVATE nlohmann_json::nlohmann_json Catch2::Catch2WithMain re2::re2 hiredis::hiredis uv_a)

# Register task namespaces for schema_delta_tests
foreach(task_source ${TASK_SOURCES})
  register_task(schema_delta_tests "${task_source}")
endforeach()

set_target_properties(schema_delta_tests PROPERTIES
  RUNTIME_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}/bin"
)

# DAG scheduler tests executable (Catch2)
add_executable(dag_scheduler_tests
  tests/test_dag_scheduler.cpp
  src/plan.cpp
  src/executor.cpp
  src/dag_scheduler.cpp
  src/async_dag_scheduler.cpp
  src/cpu_pool.cpp
  src/task_registry.cpp
  src/output_contract.cpp
  src/capability_registry.cpp
  src/writes_effect.cpp
  src/redis_client.cpp
  src/io_clients.cpp
  src/thread_pool.cpp
  src/inflight_limiter.cpp
  src/event_loop.cpp
  src/async_redis_client.cpp
  src/async_io_clients.cpp
  ${TASK_SOURCES}
)

target_include_directories(dag_scheduler_tests PRIVATE
  ${CMAKE_SOURCE_DIR}/include
  ${hiredis_SOURCE_DIR}
)
target_link_libraries(dag_scheduler_tests PRIVATE nlohmann_json::nlohmann_json Catch2::Catch2WithMain re2::re2 hiredis::hiredis uv_a)

# Register task namespaces for dag_scheduler_tests
foreach(task_source ${TASK_SOURCES})
  register_task(dag_scheduler_tests "${task_source}")
endforeach()

set_target_properties(dag_scheduler_tests PROPERTIES
  RUNTIME_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}/bin"
)

# Event loop tests executable (Catch2) - coroutine/libuv primitives
add_executable(event_loop_tests
  tests/test_event_loop.cpp
  src/event_loop.cpp
)

target_include_directories(event_loop_tests PRIVATE ${CMAKE_SOURCE_DIR}/include)
target_link_libraries(event_loop_tests PRIVATE Catch2::Catch2WithMain uv_a)

set_target_properties(event_loop_tests PROPERTIES
  RUNTIME_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}/bin"
)

# Async Redis tests executable (Catch2) - async Redis client with libuv + hiredis
add_executable(async_redis_tests
  tests/test_async_redis.cpp
  src/event_loop.cpp
  src/async_redis_client.cpp
  src/async_io_clients.cpp
)

target_include_directories(async_redis_tests PRIVATE
  ${CMAKE_SOURCE_DIR}/include
  ${hiredis_SOURCE_DIR}  # For adapters/libuv.h
)
target_link_libraries(async_redis_tests PRIVATE Catch2::Catch2WithMain uv_a hiredis::hiredis nlohmann_json::nlohmann_json)

set_target_properties(async_redis_tests PROPERTIES
  RUNTIME_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}/bin"
)
