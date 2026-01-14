cmake_minimum_required(VERSION 3.20)
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

# Disable Catch2's own tests
set(CATCH_BUILD_TESTING OFF CACHE BOOL "" FORCE)

FetchContent_MakeAvailable(json cli11 Catch2)

add_executable(rankd
  src/main.cpp
  src/plan.cpp
  src/executor.cpp
  src/task_registry.cpp
)

target_include_directories(rankd PRIVATE ${CMAKE_SOURCE_DIR}/include)
target_link_libraries(rankd PRIVATE nlohmann_json::nlohmann_json CLI11::CLI11)

# Output to engine/bin/
set_target_properties(rankd PROPERTIES
  RUNTIME_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}/bin"
)

# Test executable (Catch2)
add_executable(rankd_tests
  tests/test_rowset.cpp
  tests/test_param_table.cpp
  src/task_registry.cpp
)

target_include_directories(rankd_tests PRIVATE ${CMAKE_SOURCE_DIR}/include)
target_link_libraries(rankd_tests PRIVATE nlohmann_json::nlohmann_json Catch2::Catch2WithMain)

set_target_properties(rankd_tests PROPERTIES
  RUNTIME_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}/bin"
)
