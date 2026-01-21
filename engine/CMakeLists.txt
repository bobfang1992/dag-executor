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

# Disable Catch2's own tests
set(CATCH_BUILD_TESTING OFF CACHE BOOL "" FORCE)

# Disable RE2 tests
set(RE2_BUILD_TESTING OFF CACHE BOOL "" FORCE)

# Disable hiredis tests and SSL
set(DISABLE_TESTS ON CACHE BOOL "" FORCE)
set(ENABLE_SSL OFF CACHE BOOL "" FORCE)

FetchContent_MakeAvailable(json cli11 Catch2 re2 hiredis)

# Task source files (self-registering via static TaskRegistrar)
set(TASK_SOURCES
  src/tasks/viewer.cpp
  src/tasks/follow.cpp
  src/tasks/media.cpp
  src/tasks/recommendation.cpp
  src/tasks/take.cpp
  src/tasks/sort.cpp
  src/tasks/vm.cpp
  src/tasks/filter.cpp
  src/tasks/concat.cpp
)

add_executable(rankd
  src/main.cpp
  src/plan.cpp
  src/executor.cpp
  src/task_registry.cpp
  src/output_contract.cpp
  src/capability_registry.cpp
  src/writes_effect.cpp
  src/redis_client.cpp
  ${TASK_SOURCES}
)

target_include_directories(rankd PRIVATE ${CMAKE_SOURCE_DIR}/include)
target_link_libraries(rankd PRIVATE nlohmann_json::nlohmann_json CLI11::CLI11 re2::re2 hiredis::hiredis)

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
  src/task_registry.cpp
  src/output_contract.cpp
  src/writes_effect.cpp
  src/redis_client.cpp
  ${TASK_SOURCES}
)

target_include_directories(rankd_tests PRIVATE ${CMAKE_SOURCE_DIR}/include)
target_link_libraries(rankd_tests PRIVATE nlohmann_json::nlohmann_json Catch2::Catch2WithMain re2::re2 hiredis::hiredis)

set_target_properties(rankd_tests PROPERTIES
  RUNTIME_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}/bin"
)

# Concat tests executable (Catch2)
add_executable(concat_tests
  tests/test_concat.cpp
  src/plan.cpp
  src/executor.cpp
  src/task_registry.cpp
  src/output_contract.cpp
  src/capability_registry.cpp
  src/writes_effect.cpp
  src/redis_client.cpp
  ${TASK_SOURCES}
)

target_include_directories(concat_tests PRIVATE ${CMAKE_SOURCE_DIR}/include)
target_link_libraries(concat_tests PRIVATE nlohmann_json::nlohmann_json Catch2::Catch2WithMain re2::re2 hiredis::hiredis)

set_target_properties(concat_tests PROPERTIES
  RUNTIME_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}/bin"
)

# Regex tests executable (simple main, no Catch2)
add_executable(regex_tests
  tests/test_regex.cpp
  src/plan.cpp
  src/executor.cpp
  src/task_registry.cpp
  src/output_contract.cpp
  src/capability_registry.cpp
  src/writes_effect.cpp
  src/redis_client.cpp
  ${TASK_SOURCES}
)

target_include_directories(regex_tests PRIVATE ${CMAKE_SOURCE_DIR}/include)
target_link_libraries(regex_tests PRIVATE nlohmann_json::nlohmann_json re2::re2 hiredis::hiredis)

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
  src/task_registry.cpp
  src/output_contract.cpp
  src/capability_registry.cpp
  src/writes_effect.cpp
  src/redis_client.cpp
  ${TASK_SOURCES}
)

target_include_directories(plan_info_tests PRIVATE ${CMAKE_SOURCE_DIR}/include)
target_link_libraries(plan_info_tests PRIVATE nlohmann_json::nlohmann_json Catch2::Catch2WithMain re2::re2 hiredis::hiredis)

set_target_properties(plan_info_tests PROPERTIES
  RUNTIME_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}/bin"
)

# Runtime schema delta tests executable (Catch2)
add_executable(schema_delta_tests
  tests/test_runtime_schema_delta.cpp
  src/plan.cpp
  src/executor.cpp
  src/task_registry.cpp
  src/output_contract.cpp
  src/capability_registry.cpp
  src/writes_effect.cpp
  src/redis_client.cpp
  ${TASK_SOURCES}
)

target_include_directories(schema_delta_tests PRIVATE ${CMAKE_SOURCE_DIR}/include)
target_link_libraries(schema_delta_tests PRIVATE nlohmann_json::nlohmann_json Catch2::Catch2WithMain re2::re2 hiredis::hiredis)

set_target_properties(schema_delta_tests PROPERTIES
  RUNTIME_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}/bin"
)
