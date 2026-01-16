#include <catch2/catch_test_macros.hpp>

#include "writes_effect.h"
#include <nlohmann/json.hpp>

using namespace rankd;

TEST_CASE("EffectKeys evaluates to Exact", "[writes_effect]") {
  SECTION("Empty keys") {
    WritesEffectExpr expr = EffectKeys{};
    auto result = eval_writes(expr, {});
    REQUIRE(result.kind == EffectKind::Exact);
    REQUIRE(result.keys.empty());
  }

  SECTION("Single key") {
    WritesEffectExpr expr = EffectKeys{{1001}};
    auto result = eval_writes(expr, {});
    REQUIRE(result.kind == EffectKind::Exact);
    REQUIRE(result.keys == std::vector<uint32_t>{1001});
  }

  SECTION("Multiple keys - sorted") {
    WritesEffectExpr expr = EffectKeys{{3, 1, 2}};
    auto result = eval_writes(expr, {});
    REQUIRE(result.kind == EffectKind::Exact);
    REQUIRE(result.keys == std::vector<uint32_t>{1, 2, 3});
  }

  SECTION("Duplicate keys - deduped (set semantics)") {
    WritesEffectExpr expr = EffectKeys{{1, 2, 1, 3, 2}};
    auto result = eval_writes(expr, {});
    REQUIRE(result.kind == EffectKind::Exact);
    REQUIRE(result.keys == std::vector<uint32_t>{1, 2, 3});
  }
}

TEST_CASE("EffectFromParam with empty gamma returns Unknown", "[writes_effect]") {
  WritesEffectExpr expr = EffectFromParam{"out_key"};
  auto result = eval_writes(expr, {});
  REQUIRE(result.kind == EffectKind::Unknown);
  REQUIRE(result.keys.empty());
}

TEST_CASE("EffectFromParam with gamma returns Exact", "[writes_effect]") {
  WritesEffectExpr expr = EffectFromParam{"out_key"};
  EffectGamma gamma;
  gamma["out_key"] = uint32_t{1001};
  auto result = eval_writes(expr, gamma);
  REQUIRE(result.kind == EffectKind::Exact);
  REQUIRE(result.keys == std::vector<uint32_t>{1001});
}

TEST_CASE("EffectFromParam with wrong type in gamma returns Unknown",
          "[writes_effect]") {
  WritesEffectExpr expr = EffectFromParam{"out_key"};
  EffectGamma gamma;
  gamma["out_key"] = std::string{"not_a_key_id"};
  auto result = eval_writes(expr, gamma);
  REQUIRE(result.kind == EffectKind::Unknown);
}

TEST_CASE("EffectSwitchEnum with matching case", "[writes_effect]") {
  std::map<std::string, std::shared_ptr<WritesEffectExpr>> cases;
  cases["esr"] = makeEffectKeys({4001});
  cases["lsr"] = makeEffectKeys({4002});

  WritesEffectExpr expr = EffectSwitchEnum{"stage", std::move(cases)};
  EffectGamma gamma;
  gamma["stage"] = std::string{"esr"};

  auto result = eval_writes(expr, gamma);
  REQUIRE(result.kind == EffectKind::Exact);
  REQUIRE(result.keys == std::vector<uint32_t>{4001});
}

TEST_CASE("EffectSwitchEnum with different case", "[writes_effect]") {
  std::map<std::string, std::shared_ptr<WritesEffectExpr>> cases;
  cases["esr"] = makeEffectKeys({4001});
  cases["lsr"] = makeEffectKeys({4002});

  WritesEffectExpr expr = EffectSwitchEnum{"stage", std::move(cases)};
  EffectGamma gamma;
  gamma["stage"] = std::string{"lsr"};

  auto result = eval_writes(expr, gamma);
  REQUIRE(result.kind == EffectKind::Exact);
  REQUIRE(result.keys == std::vector<uint32_t>{4002});
}

TEST_CASE("EffectSwitchEnum with unknown param returns May(union)",
          "[writes_effect]") {
  std::map<std::string, std::shared_ptr<WritesEffectExpr>> cases;
  cases["esr"] = makeEffectKeys({4001});
  cases["lsr"] = makeEffectKeys({4002});

  WritesEffectExpr expr = EffectSwitchEnum{"stage", std::move(cases)};
  EffectGamma gamma; // No "stage" in gamma

  auto result = eval_writes(expr, gamma);
  REQUIRE(result.kind == EffectKind::May);
  REQUIRE(result.keys == std::vector<uint32_t>{4001, 4002});
}

TEST_CASE("EffectSwitchEnum with missing case returns Unknown",
          "[writes_effect]") {
  std::map<std::string, std::shared_ptr<WritesEffectExpr>> cases;
  cases["esr"] = makeEffectKeys({4001});

  WritesEffectExpr expr = EffectSwitchEnum{"stage", std::move(cases)};
  EffectGamma gamma;
  gamma["stage"] = std::string{"unknown_stage"};

  auto result = eval_writes(expr, gamma);
  REQUIRE(result.kind == EffectKind::Unknown);
}

TEST_CASE("EffectUnion combines Exact results to Exact", "[writes_effect]") {
  std::vector<std::shared_ptr<WritesEffectExpr>> items;
  items.push_back(makeEffectKeys({1, 2}));
  items.push_back(makeEffectKeys({3, 4}));

  WritesEffectExpr expr = EffectUnion{std::move(items)};
  auto result = eval_writes(expr, {});

  REQUIRE(result.kind == EffectKind::Exact);
  REQUIRE(result.keys == std::vector<uint32_t>{1, 2, 3, 4});
}

TEST_CASE("EffectUnion with May results in May", "[writes_effect]") {
  std::map<std::string, std::shared_ptr<WritesEffectExpr>> cases;
  cases["a"] = makeEffectKeys({1});
  cases["b"] = makeEffectKeys({2});

  std::vector<std::shared_ptr<WritesEffectExpr>> items;
  items.push_back(makeEffectKeys({10}));
  items.push_back(makeEffectSwitchEnum("param", std::move(cases)));

  WritesEffectExpr expr = EffectUnion{std::move(items)};
  EffectGamma gamma; // param not in gamma => May

  auto result = eval_writes(expr, gamma);
  REQUIRE(result.kind == EffectKind::May);
  REQUIRE(result.keys == std::vector<uint32_t>{1, 2, 10});
}

TEST_CASE("EffectUnion with Unknown results in Unknown", "[writes_effect]") {
  std::vector<std::shared_ptr<WritesEffectExpr>> items;
  items.push_back(makeEffectKeys({1}));
  items.push_back(makeEffectFromParam("unknown_param"));

  WritesEffectExpr expr = EffectUnion{std::move(items)};
  auto result = eval_writes(expr, {});

  REQUIRE(result.kind == EffectKind::Unknown);
  REQUIRE(result.keys.empty());
}

TEST_CASE("Empty EffectUnion returns Exact empty", "[writes_effect]") {
  WritesEffectExpr expr = EffectUnion{};
  auto result = eval_writes(expr, {});
  REQUIRE(result.kind == EffectKind::Exact);
  REQUIRE(result.keys.empty());
}

TEST_CASE("serialize_writes_effect for EffectKeys", "[writes_effect]") {
  WritesEffectExpr expr = EffectKeys{{3, 1, 2}};
  std::string json = serialize_writes_effect(expr);
  auto parsed = nlohmann::json::parse(json);

  REQUIRE(parsed["kind"] == "Keys");
  REQUIRE(parsed["key_ids"] == nlohmann::json::array({1, 2, 3})); // sorted
}

TEST_CASE("serialize_writes_effect for EffectFromParam", "[writes_effect]") {
  WritesEffectExpr expr = EffectFromParam{"out_key"};
  std::string json = serialize_writes_effect(expr);
  auto parsed = nlohmann::json::parse(json);

  REQUIRE(parsed["kind"] == "FromParam");
  REQUIRE(parsed["param"] == "out_key");
}

TEST_CASE("serialize_writes_effect for EffectSwitchEnum", "[writes_effect]") {
  std::map<std::string, std::shared_ptr<WritesEffectExpr>> cases;
  cases["esr"] = makeEffectKeys({4001});
  cases["lsr"] = makeEffectKeys({4002});

  WritesEffectExpr expr = EffectSwitchEnum{"stage", std::move(cases)};
  std::string json = serialize_writes_effect(expr);
  auto parsed = nlohmann::json::parse(json);

  REQUIRE(parsed["kind"] == "SwitchEnum");
  REQUIRE(parsed["param"] == "stage");
  REQUIRE(parsed["cases"]["esr"]["kind"] == "Keys");
  REQUIRE(parsed["cases"]["esr"]["key_ids"] == nlohmann::json::array({4001}));
  REQUIRE(parsed["cases"]["lsr"]["kind"] == "Keys");
  REQUIRE(parsed["cases"]["lsr"]["key_ids"] == nlohmann::json::array({4002}));
}

TEST_CASE("serialize_writes_effect for EffectUnion", "[writes_effect]") {
  std::vector<std::shared_ptr<WritesEffectExpr>> items;
  items.push_back(makeEffectKeys({1}));
  items.push_back(makeEffectFromParam("p"));

  WritesEffectExpr expr = EffectUnion{std::move(items)};
  std::string json = serialize_writes_effect(expr);
  auto parsed = nlohmann::json::parse(json);

  REQUIRE(parsed["kind"] == "Union");
  REQUIRE(parsed["items"].size() == 2);
  REQUIRE(parsed["items"][0]["kind"] == "Keys");
  REQUIRE(parsed["items"][1]["kind"] == "FromParam");
}

TEST_CASE("Nested SwitchEnum in Union", "[writes_effect]") {
  std::map<std::string, std::shared_ptr<WritesEffectExpr>> inner_cases;
  inner_cases["x"] = makeEffectKeys({100});
  inner_cases["y"] = makeEffectKeys({200});

  std::vector<std::shared_ptr<WritesEffectExpr>> items;
  items.push_back(makeEffectKeys({1}));
  items.push_back(makeEffectSwitchEnum("inner", std::move(inner_cases)));

  WritesEffectExpr expr = EffectUnion{std::move(items)};

  SECTION("With known inner param") {
    EffectGamma gamma;
    gamma["inner"] = std::string{"x"};
    auto result = eval_writes(expr, gamma);
    REQUIRE(result.kind == EffectKind::Exact);
    REQUIRE(result.keys == std::vector<uint32_t>{1, 100});
  }

  SECTION("With unknown inner param") {
    EffectGamma gamma;
    auto result = eval_writes(expr, gamma);
    REQUIRE(result.kind == EffectKind::May);
    REQUIRE(result.keys == std::vector<uint32_t>{1, 100, 200});
  }
}
