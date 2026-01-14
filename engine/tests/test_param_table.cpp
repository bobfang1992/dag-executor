#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include "param_table.h"
#include <cmath>

using namespace rankd;
using Catch::Matchers::ContainsSubstring;

TEST_CASE("ParamTable basic set/get operations", "[param_table]") {
  ParamTable table;

  SECTION("initially empty") {
    REQUIRE_FALSE(table.has(ParamId::media_age_penalty_weight));
    REQUIRE_FALSE(table.getFloat(ParamId::media_age_penalty_weight).has_value());
  }

  SECTION("set and get float") {
    table.set(ParamId::media_age_penalty_weight, 0.5);
    REQUIRE(table.has(ParamId::media_age_penalty_weight));
    REQUIRE(table.getFloat(ParamId::media_age_penalty_weight).has_value());
    REQUIRE(table.getFloat(ParamId::media_age_penalty_weight).value() == 0.5);
    REQUIRE_FALSE(table.isNull(ParamId::media_age_penalty_weight));
  }

  SECTION("set and get string") {
    table.set(ParamId::blocklist_regex, std::string("test.*"));
    REQUIRE(table.has(ParamId::blocklist_regex));
    REQUIRE(table.getString(ParamId::blocklist_regex).has_value());
    REQUIRE(table.getString(ParamId::blocklist_regex).value() == "test.*");
  }
}

TEST_CASE("ParamTable null value handling", "[param_table]") {
  ParamTable table;

  table.set(ParamId::blocklist_regex, NullTag{});

  REQUIRE(table.has(ParamId::blocklist_regex));
  REQUIRE(table.isNull(ParamId::blocklist_regex));
  REQUIRE_FALSE(table.getString(ParamId::blocklist_regex).has_value());
}

TEST_CASE("ParamTable::fromParamOverrides with valid input", "[param_table]") {
  nlohmann::json overrides;
  overrides["media_age_penalty_weight"] = 0.35;
  overrides["esr_cutoff"] = 2.5;

  auto table = ParamTable::fromParamOverrides(overrides);

  REQUIRE(table.has(ParamId::media_age_penalty_weight));
  REQUIRE(table.getFloat(ParamId::media_age_penalty_weight).value() == 0.35);
  REQUIRE(table.has(ParamId::esr_cutoff));
  REQUIRE(table.getFloat(ParamId::esr_cutoff).value() == 2.5);
}

TEST_CASE("ParamTable::fromParamOverrides nullable params", "[param_table]") {
  SECTION("null for nullable param") {
    nlohmann::json overrides;
    overrides["blocklist_regex"] = nullptr;

    auto table = ParamTable::fromParamOverrides(overrides);
    REQUIRE(table.has(ParamId::blocklist_regex));
    REQUIRE(table.isNull(ParamId::blocklist_regex));
  }

  SECTION("value for nullable param") {
    nlohmann::json overrides;
    overrides["blocklist_regex"] = "foo.*";

    auto table = ParamTable::fromParamOverrides(overrides);
    REQUIRE(table.has(ParamId::blocklist_regex));
    REQUIRE_FALSE(table.isNull(ParamId::blocklist_regex));
    REQUIRE(table.getString(ParamId::blocklist_regex).value() == "foo.*");
  }
}

TEST_CASE("ParamTable::fromParamOverrides rejects unknown param",
          "[param_table]") {
  nlohmann::json overrides;
  overrides["unknown_param"] = 42;

  REQUIRE_THROWS_WITH(ParamTable::fromParamOverrides(overrides),
                      ContainsSubstring("unknown param") &&
                          ContainsSubstring("unknown_param"));
}

TEST_CASE("ParamTable::fromParamOverrides rejects wrong type", "[param_table]") {
  nlohmann::json overrides;
  overrides["media_age_penalty_weight"] = "not a number";

  REQUIRE_THROWS_WITH(ParamTable::fromParamOverrides(overrides),
                      ContainsSubstring("must be float"));
}

TEST_CASE("ParamTable::fromParamOverrides rejects null for non-nullable",
          "[param_table]") {
  nlohmann::json overrides;
  overrides["media_age_penalty_weight"] = nullptr;

  REQUIRE_THROWS_WITH(ParamTable::fromParamOverrides(overrides),
                      ContainsSubstring("cannot be null"));
}

TEST_CASE("ParamTable::fromParamOverrides rejects non-finite floats",
          "[param_table]") {
  SECTION("infinity") {
    nlohmann::json overrides;
    overrides["media_age_penalty_weight"] =
        std::numeric_limits<double>::infinity();

    REQUIRE_THROWS_WITH(ParamTable::fromParamOverrides(overrides),
                        ContainsSubstring("finite"));
  }

  SECTION("NaN") {
    nlohmann::json overrides;
    overrides["media_age_penalty_weight"] = std::nan("");

    REQUIRE_THROWS_WITH(ParamTable::fromParamOverrides(overrides),
                        ContainsSubstring("finite"));
  }
}

TEST_CASE("validateInt handles overflow", "[param_table]") {
  SECTION("rejects uint64 exceeding int64 max") {
    nlohmann::json large_uint =
        static_cast<uint64_t>(std::numeric_limits<int64_t>::max()) + 1;

    REQUIRE_THROWS_WITH(validateInt(large_uint, "test_param"),
                        ContainsSubstring("out of int64 range"));
  }

  SECTION("accepts int64 max") {
    nlohmann::json max_int = std::numeric_limits<int64_t>::max();
    REQUIRE(validateInt(max_int, "test_param") ==
            std::numeric_limits<int64_t>::max());
  }

  SECTION("accepts negative integers") {
    nlohmann::json neg_int = -42;
    REQUIRE(validateInt(neg_int, "test_param") == -42);
  }
}

TEST_CASE("ParamTable::fromParamOverrides with empty/null input",
          "[param_table]") {
  SECTION("empty object") {
    nlohmann::json overrides = nlohmann::json::object();
    auto table = ParamTable::fromParamOverrides(overrides);

    REQUIRE_FALSE(table.has(ParamId::media_age_penalty_weight));
    REQUIRE_FALSE(table.has(ParamId::blocklist_regex));
    REQUIRE_FALSE(table.has(ParamId::esr_cutoff));
  }

  SECTION("null") {
    nlohmann::json overrides = nullptr;
    auto table = ParamTable::fromParamOverrides(overrides);

    REQUIRE_FALSE(table.has(ParamId::media_age_penalty_weight));
  }
}
