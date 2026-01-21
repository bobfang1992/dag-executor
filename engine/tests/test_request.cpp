#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include "request.h"

using namespace rankd;
using Catch::Matchers::ContainsSubstring;

TEST_CASE("parse_user_id accepts valid integers", "[request]") {
  std::string error;

  SECTION("positive integer") {
    nlohmann::json val = 123;
    auto result = parse_user_id(val, error);
    REQUIRE(result.has_value());
    REQUIRE(result.value() == 123);
  }

  SECTION("integer 1 (minimum valid)") {
    nlohmann::json val = 1;
    auto result = parse_user_id(val, error);
    REQUIRE(result.has_value());
    REQUIRE(result.value() == 1);
  }

  SECTION("large integer near uint32 max") {
    nlohmann::json val = 4294967295u; // UINT32_MAX
    auto result = parse_user_id(val, error);
    REQUIRE(result.has_value());
    REQUIRE(result.value() == 4294967295u);
  }
}

TEST_CASE("parse_user_id accepts valid string integers", "[request]") {
  std::string error;

  SECTION("string \"123\"") {
    nlohmann::json val = "123";
    auto result = parse_user_id(val, error);
    REQUIRE(result.has_value());
    REQUIRE(result.value() == 123);
  }

  SECTION("string \"1\"") {
    nlohmann::json val = "1";
    auto result = parse_user_id(val, error);
    REQUIRE(result.has_value());
    REQUIRE(result.value() == 1);
  }

  SECTION("string at uint32 max") {
    nlohmann::json val = "4294967295";
    auto result = parse_user_id(val, error);
    REQUIRE(result.has_value());
    REQUIRE(result.value() == 4294967295u);
  }
}

TEST_CASE("parse_user_id rejects invalid types", "[request]") {
  std::string error;

  SECTION("null") {
    nlohmann::json val = nullptr;
    auto result = parse_user_id(val, error);
    REQUIRE_FALSE(result.has_value());
    REQUIRE_THAT(error, ContainsSubstring("null"));
  }

  SECTION("boolean") {
    nlohmann::json val = true;
    auto result = parse_user_id(val, error);
    REQUIRE_FALSE(result.has_value());
    REQUIRE_THAT(error, ContainsSubstring("boolean"));
  }

  SECTION("float") {
    nlohmann::json val = 3.14;
    auto result = parse_user_id(val, error);
    REQUIRE_FALSE(result.has_value());
    REQUIRE_THAT(error, ContainsSubstring("float"));
  }

  SECTION("object") {
    nlohmann::json val = nlohmann::json::object();
    auto result = parse_user_id(val, error);
    REQUIRE_FALSE(result.has_value());
    REQUIRE_THAT(error, ContainsSubstring("object"));
  }

  SECTION("array") {
    nlohmann::json val = nlohmann::json::array();
    auto result = parse_user_id(val, error);
    REQUIRE_FALSE(result.has_value());
    REQUIRE_THAT(error, ContainsSubstring("array"));
  }
}

TEST_CASE("parse_user_id rejects invalid values", "[request]") {
  std::string error;

  SECTION("zero") {
    nlohmann::json val = 0;
    auto result = parse_user_id(val, error);
    REQUIRE_FALSE(result.has_value());
    REQUIRE_THAT(error, ContainsSubstring("positive"));
  }

  SECTION("negative") {
    nlohmann::json val = -5;
    auto result = parse_user_id(val, error);
    REQUIRE_FALSE(result.has_value());
    REQUIRE_THAT(error, ContainsSubstring("positive"));
  }

  SECTION("exceeds uint32 max") {
    nlohmann::json val = 4294967296LL; // UINT32_MAX + 1
    auto result = parse_user_id(val, error);
    REQUIRE_FALSE(result.has_value());
    REQUIRE_THAT(error, ContainsSubstring("exceeds"));
  }

  SECTION("string \"0\"") {
    nlohmann::json val = "0";
    auto result = parse_user_id(val, error);
    REQUIRE_FALSE(result.has_value());
    REQUIRE_THAT(error, ContainsSubstring("positive"));
  }

  SECTION("empty string") {
    nlohmann::json val = "";
    auto result = parse_user_id(val, error);
    REQUIRE_FALSE(result.has_value());
    REQUIRE_THAT(error, ContainsSubstring("empty"));
  }

  SECTION("non-numeric string") {
    nlohmann::json val = "abc";
    auto result = parse_user_id(val, error);
    REQUIRE_FALSE(result.has_value());
    REQUIRE_THAT(error, ContainsSubstring("not a valid decimal"));
  }

  SECTION("string with leading zeros") {
    nlohmann::json val = "0123";
    auto result = parse_user_id(val, error);
    // from_chars accepts leading zeros, so "0123" parses as 123
    REQUIRE(result.has_value());
    REQUIRE(result.value() == 123);
  }

  SECTION("string exceeds uint32 max") {
    nlohmann::json val = "4294967296";
    auto result = parse_user_id(val, error);
    REQUIRE_FALSE(result.has_value());
    REQUIRE_THAT(error, ContainsSubstring("exceeds"));
  }

  SECTION("string with spaces") {
    nlohmann::json val = " 123";
    auto result = parse_user_id(val, error);
    REQUIRE_FALSE(result.has_value());
    REQUIRE_THAT(error, ContainsSubstring("not a valid decimal"));
  }

  SECTION("string with trailing text") {
    nlohmann::json val = "123abc";
    auto result = parse_user_id(val, error);
    REQUIRE_FALSE(result.has_value());
    REQUIRE_THAT(error, ContainsSubstring("not a valid decimal"));
  }
}

TEST_CASE("parse_request_context with valid input", "[request]") {
  SECTION("minimal valid request") {
    nlohmann::json request;
    request["user_id"] = 123;

    auto result = parse_request_context(request);
    REQUIRE(result.ok);
    REQUIRE(result.context.user_id == 123);
    REQUIRE_FALSE(result.context.request_id.empty()); // auto-generated
  }

  SECTION("with explicit request_id") {
    nlohmann::json request;
    request["user_id"] = 456;
    request["request_id"] = "my-request-id";

    auto result = parse_request_context(request);
    REQUIRE(result.ok);
    REQUIRE(result.context.user_id == 456);
    REQUIRE(result.context.request_id == "my-request-id");
  }

  SECTION("user_id as string") {
    nlohmann::json request;
    request["user_id"] = "789";

    auto result = parse_request_context(request);
    REQUIRE(result.ok);
    REQUIRE(result.context.user_id == 789);
  }
}

TEST_CASE("parse_request_context with invalid input", "[request]") {
  SECTION("missing user_id") {
    nlohmann::json request = nlohmann::json::object();

    auto result = parse_request_context(request);
    REQUIRE_FALSE(result.ok);
    REQUIRE_THAT(result.error, ContainsSubstring("missing required field"));
  }

  SECTION("null user_id") {
    nlohmann::json request;
    request["user_id"] = nullptr;

    auto result = parse_request_context(request);
    REQUIRE_FALSE(result.ok);
    REQUIRE_THAT(result.error, ContainsSubstring("null"));
  }

  SECTION("invalid user_id type") {
    nlohmann::json request;
    request["user_id"] = 3.14;

    auto result = parse_request_context(request);
    REQUIRE_FALSE(result.ok);
    REQUIRE_THAT(result.error, ContainsSubstring("float"));
  }

  SECTION("user_id = 0") {
    nlohmann::json request;
    request["user_id"] = 0;

    auto result = parse_request_context(request);
    REQUIRE_FALSE(result.ok);
    REQUIRE_THAT(result.error, ContainsSubstring("positive"));
  }

  SECTION("negative user_id") {
    nlohmann::json request;
    request["user_id"] = -1;

    auto result = parse_request_context(request);
    REQUIRE_FALSE(result.ok);
    REQUIRE_THAT(result.error, ContainsSubstring("positive"));
  }
}
