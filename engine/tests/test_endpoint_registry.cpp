#include <algorithm>
#include <catch2/catch_test_macros.hpp>
#include "endpoint_registry.h"
#include <fstream>
#include <iostream>
#include <nlohmann/json.hpp>

using namespace rankd;

// Helper to write temp JSON file for testing
static std::string write_temp_json(const nlohmann::json& j, const std::string& suffix) {
  static int counter = 0;
  std::string path = "/tmp/endpoint_test_" + std::to_string(counter++) + "_" + suffix + ".json";
  std::ofstream f(path);
  f << j.dump(2);
  return path;
}

static nlohmann::json sort_endpoints_json(nlohmann::json endpoints) {
  std::sort(endpoints.begin(), endpoints.end(), [](const nlohmann::json& a, const nlohmann::json& b) {
    return a.at("endpoint_id").get<std::string>() < b.at("endpoint_id").get<std::string>();
  });
  return endpoints;
}

// Attach registry/config digests to JSON (mirrors codegen + C++ loader)
static void add_endpoint_digests(nlohmann::json& j) {
  try {
    std::vector<rankd::EndpointSpec> specs;
    for (const auto& ep : j.at("endpoints")) {
      rankd::EndpointSpec spec;
      spec.endpoint_id = ep.at("endpoint_id").get<std::string>();
      spec.name = ep.at("name").get<std::string>();
      auto kind_opt = rankd::string_to_endpoint_kind(ep.at("kind").get<std::string>());
      if (!kind_opt) throw std::runtime_error("unknown endpoint kind");
      spec.kind = *kind_opt;

      const auto& resolver = ep.at("resolver");
      auto resolver_type_opt = rankd::string_to_resolver_type(resolver.at("type").get<std::string>());
      if (!resolver_type_opt) throw std::runtime_error("unknown resolver type");
      spec.resolver_type = *resolver_type_opt;
      spec.static_resolver.host = resolver.at("host").get<std::string>();
      spec.static_resolver.port = resolver.at("port").get<int>();

      if (ep.contains("policy")) {
        const auto& policy = ep.at("policy");
        if (policy.contains("max_inflight")) {
          spec.policy.max_inflight = policy.at("max_inflight").get<int>();
        }
        if (policy.contains("connect_timeout_ms")) {
          spec.policy.connect_timeout_ms = policy.at("connect_timeout_ms").get<int>();
        }
        if (policy.contains("request_timeout_ms")) {
          spec.policy.request_timeout_ms = policy.at("request_timeout_ms").get<int>();
        }
      }

      specs.push_back(std::move(spec));
    }

    j["registry_digest"] = rankd::compute_digest(rankd::registry_canonical_json(specs));
    j["config_digest"] = rankd::compute_digest(rankd::config_canonical_json(specs));
  } catch (...) {
    // For malformed fixtures (expected to fail before digest check), attach dummy digests.
    j["registry_digest"] = "invalid";
    j["config_digest"] = "invalid";
  }
}

TEST_CASE("EndpointRegistry loads valid JSON", "[endpoint_registry]") {
  nlohmann::json j = {
    {"schema_version", 1},
    {"env", "dev"},
    {"endpoints", nlohmann::json::array({
      {
        {"endpoint_id", "ep_0001"},
        {"name", "redis_default"},
        {"kind", "redis"},
        {"resolver", {{"type", "static"}, {"host", "127.0.0.1"}, {"port", 6379}}},
        {"policy", {{"max_inflight", 64}}}
      },
      {
        {"endpoint_id", "ep_0002"},
        {"name", "http_api"},
        {"kind", "http"},
        {"resolver", {{"type", "static"}, {"host", "localhost"}, {"port", 8080}}},
        {"policy", {}}
      }
    })}
  };

  add_endpoint_digests(j);

  const std::string expected_registry_digest = j["registry_digest"].get<std::string>();
  const std::string expected_config_digest = j["config_digest"].get<std::string>();

  std::string path = write_temp_json(j, "valid");
  auto result = EndpointRegistry::LoadFromJson(path, "dev");

  if (std::holds_alternative<std::string>(result)) {
    std::cerr << "LoadFromJson error: " << std::get<std::string>(result) << std::endl;
  }
  REQUIRE(std::holds_alternative<EndpointRegistry>(result));

  const auto& reg = std::get<EndpointRegistry>(result);

  SECTION("basic properties") {
    REQUIRE(reg.env() == "dev");
    REQUIRE(reg.registry_digest() == expected_registry_digest);
    REQUIRE(reg.config_digest() == expected_config_digest);
    REQUIRE(reg.size() == 2);
  }

  SECTION("by_id lookup") {
    const auto* ep1 = reg.by_id("ep_0001");
    REQUIRE(ep1 != nullptr);
    REQUIRE(ep1->name == "redis_default");
    REQUIRE(ep1->kind == EndpointKind::Redis);
    REQUIRE(ep1->resolver_type == ResolverType::Static);
    REQUIRE(ep1->static_resolver.host == "127.0.0.1");
    REQUIRE(ep1->static_resolver.port == 6379);
    REQUIRE(ep1->policy.max_inflight == 64);

    const auto* ep2 = reg.by_id("ep_0002");
    REQUIRE(ep2 != nullptr);
    REQUIRE(ep2->name == "http_api");
    REQUIRE(ep2->kind == EndpointKind::Http);

    const auto* unknown = reg.by_id("ep_9999");
    REQUIRE(unknown == nullptr);
  }

  SECTION("by_name lookup") {
    const auto* ep = reg.by_name("redis_default");
    REQUIRE(ep != nullptr);
    REQUIRE(ep->endpoint_id == "ep_0001");

    const auto* unknown = reg.by_name("nonexistent");
    REQUIRE(unknown == nullptr);
  }
}

TEST_CASE("EndpointRegistry rejects duplicate endpoint_id", "[endpoint_registry]") {
  nlohmann::json j = {
    {"schema_version", 1},
    {"env", "dev"},
    {"registry_digest", "abc"},
    {"config_digest", "def"},
    {"endpoints", nlohmann::json::array({
      {{"endpoint_id", "ep_0001"}, {"name", "redis1"}, {"kind", "redis"},
       {"resolver", {{"type", "static"}, {"host", "127.0.0.1"}, {"port", 6379}}}},
      {{"endpoint_id", "ep_0001"}, {"name", "redis2"}, {"kind", "redis"},
       {"resolver", {{"type", "static"}, {"host", "127.0.0.1"}, {"port", 6380}}}}
    })}
  };

  add_endpoint_digests(j);

  std::string path = write_temp_json(j, "dup_id");
  auto result = EndpointRegistry::LoadFromJson(path);

  REQUIRE(std::holds_alternative<std::string>(result));
  REQUIRE(std::get<std::string>(result).find("Duplicate endpoint_id") != std::string::npos);
}

TEST_CASE("EndpointRegistry rejects duplicate name", "[endpoint_registry]") {
  nlohmann::json j = {
    {"schema_version", 1},
    {"env", "dev"},
    {"registry_digest", "abc"},
    {"config_digest", "def"},
    {"endpoints", nlohmann::json::array({
      {{"endpoint_id", "ep_0001"}, {"name", "same_name"}, {"kind", "redis"},
       {"resolver", {{"type", "static"}, {"host", "127.0.0.1"}, {"port", 6379}}}},
      {{"endpoint_id", "ep_0002"}, {"name", "same_name"}, {"kind", "http"},
       {"resolver", {{"type", "static"}, {"host", "127.0.0.1"}, {"port", 8080}}}}
    })}
  };

  add_endpoint_digests(j);

  std::string path = write_temp_json(j, "dup_name");
  auto result = EndpointRegistry::LoadFromJson(path);

  REQUIRE(std::holds_alternative<std::string>(result));
  REQUIRE(std::get<std::string>(result).find("Duplicate endpoint name") != std::string::npos);
}

TEST_CASE("EndpointRegistry rejects invalid port", "[endpoint_registry]") {
  SECTION("port = 0") {
    nlohmann::json j = {
      {"schema_version", 1},
      {"env", "dev"},
      {"endpoints", nlohmann::json::array({
        {{"endpoint_id", "ep_0001"}, {"name", "bad"}, {"kind", "redis"},
         {"resolver", {{"type", "static"}, {"host", "127.0.0.1"}, {"port", 0}}}}
      })}
    };

    add_endpoint_digests(j);

    std::string path = write_temp_json(j, "bad_port_0");
    auto result = EndpointRegistry::LoadFromJson(path);

    REQUIRE(std::holds_alternative<std::string>(result));
    REQUIRE(std::get<std::string>(result).find("invalid port") != std::string::npos);
  }

  SECTION("port = 70000") {
    nlohmann::json j = {
      {"schema_version", 1},
      {"env", "dev"},
      {"endpoints", nlohmann::json::array({
        {{"endpoint_id", "ep_0001"}, {"name", "bad"}, {"kind", "redis"},
         {"resolver", {{"type", "static"}, {"host", "127.0.0.1"}, {"port", 70000}}}}
      })}
    };

    add_endpoint_digests(j);

    std::string path = write_temp_json(j, "bad_port_70000");
    auto result = EndpointRegistry::LoadFromJson(path);

    REQUIRE(std::holds_alternative<std::string>(result));
    REQUIRE(std::get<std::string>(result).find("invalid port") != std::string::npos);
  }
}

TEST_CASE("EndpointRegistry rejects unknown kind", "[endpoint_registry]") {
  nlohmann::json j = {
    {"schema_version", 1},
    {"env", "dev"},
    {"endpoints", nlohmann::json::array({
      {{"endpoint_id", "ep_0001"}, {"name", "bad"}, {"kind", "kafka"},
       {"resolver", {{"type", "static"}, {"host", "127.0.0.1"}, {"port", 9092}}}}
    })}
  };

  add_endpoint_digests(j);

  std::string path = write_temp_json(j, "unknown_kind");
  auto result = EndpointRegistry::LoadFromJson(path);

  REQUIRE(std::holds_alternative<std::string>(result));
  REQUIRE(std::get<std::string>(result).find("unknown kind") != std::string::npos);
}

TEST_CASE("EndpointRegistry rejects non-static resolver", "[endpoint_registry]") {
  nlohmann::json j = {
    {"schema_version", 1},
    {"env", "dev"},
    {"endpoints", nlohmann::json::array({
      {{"endpoint_id", "ep_0001"}, {"name", "consul_ep"}, {"kind", "redis"},
       {"resolver", {{"type", "consul"}, {"service", "redis"}}}}
    })}
  };

  add_endpoint_digests(j);

  std::string path = write_temp_json(j, "consul_resolver");
  auto result = EndpointRegistry::LoadFromJson(path);

  REQUIRE(std::holds_alternative<std::string>(result));
  REQUIRE(std::get<std::string>(result).find("only 'static' resolver supported") != std::string::npos);
}

TEST_CASE("EndpointRegistry rejects invalid endpoint_id format", "[endpoint_registry]") {
  SECTION("missing ep_ prefix") {
    nlohmann::json j = {
      {"schema_version", 1},
      {"env", "dev"},
      {"endpoints", nlohmann::json::array({
        {{"endpoint_id", "0001"}, {"name", "bad"}, {"kind", "redis"},
         {"resolver", {{"type", "static"}, {"host", "127.0.0.1"}, {"port", 6379}}}}
      })}
    };

    add_endpoint_digests(j);

    std::string path = write_temp_json(j, "no_prefix");
    auto result = EndpointRegistry::LoadFromJson(path);

    REQUIRE(std::holds_alternative<std::string>(result));
    REQUIRE(std::get<std::string>(result).find("must start with 'ep_'") != std::string::npos);
  }

  SECTION("endpoint_id too long") {
    std::string long_id = "ep_" + std::string(100, 'x');
    nlohmann::json j = {
      {"schema_version", 1},
      {"env", "dev"},
      {"endpoints", nlohmann::json::array({
        {{"endpoint_id", long_id}, {"name", "bad"}, {"kind", "redis"},
         {"resolver", {{"type", "static"}, {"host", "127.0.0.1"}, {"port", 6379}}}}
      })}
    };

    add_endpoint_digests(j);

    std::string path = write_temp_json(j, "too_long");
    auto result = EndpointRegistry::LoadFromJson(path);

    REQUIRE(std::holds_alternative<std::string>(result));
    REQUIRE(std::get<std::string>(result).find("too long") != std::string::npos);
  }
}

TEST_CASE("EndpointRegistry rejects env mismatch", "[endpoint_registry]") {
  nlohmann::json j = {
    {"schema_version", 1},
    {"env", "dev"},
    {"endpoints", nlohmann::json::array({
      {{"endpoint_id", "ep_0001"}, {"name", "redis"}, {"kind", "redis"},
       {"resolver", {{"type", "static"}, {"host", "127.0.0.1"}, {"port", 6379}}}}
    })}
  };

  add_endpoint_digests(j);
  std::string path = write_temp_json(j, "env_mismatch");

  auto result = EndpointRegistry::LoadFromJson(path, "prod");
  REQUIRE(std::holds_alternative<std::string>(result));
  REQUIRE(std::get<std::string>(result).find("Env mismatch") != std::string::npos);
}

TEST_CASE("EndpointRegistry rejects digest mismatch", "[endpoint_registry]") {
  nlohmann::json j = {
    {"schema_version", 1},
    {"env", "dev"},
    {"endpoints", nlohmann::json::array({
      {{"endpoint_id", "ep_0001"}, {"name", "redis"}, {"kind", "redis"},
       {"resolver", {{"type", "static"}, {"host", "127.0.0.1"}, {"port", 6379}}}}
    })}
  };

  add_endpoint_digests(j);
  j["registry_digest"] = "bad_digest";
  std::string path = write_temp_json(j, "digest_mismatch");

  auto result = EndpointRegistry::LoadFromJson(path);
  REQUIRE(std::holds_alternative<std::string>(result));
  REQUIRE(std::get<std::string>(result).find("registry_digest mismatch") != std::string::npos);
}

TEST_CASE("EndpointRegistry helper functions work", "[endpoint_registry]") {
  SECTION("endpoint_kind_to_string") {
    REQUIRE(endpoint_kind_to_string(EndpointKind::Redis) == "redis");
    REQUIRE(endpoint_kind_to_string(EndpointKind::Http) == "http");
  }

  SECTION("string_to_endpoint_kind") {
    REQUIRE(string_to_endpoint_kind("redis") == EndpointKind::Redis);
    REQUIRE(string_to_endpoint_kind("http") == EndpointKind::Http);
    REQUIRE(string_to_endpoint_kind("kafka") == std::nullopt);
  }

  SECTION("resolver_type_to_string") {
    REQUIRE(resolver_type_to_string(ResolverType::Static) == "static");
    REQUIRE(resolver_type_to_string(ResolverType::Consul) == "consul");
    REQUIRE(resolver_type_to_string(ResolverType::DnsSrv) == "dns_srv");
    REQUIRE(resolver_type_to_string(ResolverType::Https) == "https");
  }

  SECTION("string_to_resolver_type") {
    REQUIRE(string_to_resolver_type("static") == ResolverType::Static);
    REQUIRE(string_to_resolver_type("consul") == ResolverType::Consul);
    REQUIRE(string_to_resolver_type("dns_srv") == ResolverType::DnsSrv);
    REQUIRE(string_to_resolver_type("https") == ResolverType::Https);
    REQUIRE(string_to_resolver_type("unknown") == std::nullopt);
  }
}

TEST_CASE("EndpointRegistry loads real generated JSON", "[endpoint_registry][integration]") {
  // Load the actual generated endpoints.dev.json
  auto result = EndpointRegistry::LoadFromJson("artifacts/endpoints.dev.json");

  REQUIRE(std::holds_alternative<EndpointRegistry>(result));

  const auto& reg = std::get<EndpointRegistry>(result);
  REQUIRE(reg.env() == "dev");
  REQUIRE(reg.size() == 2);

  // Check redis_default endpoint
  const auto* redis = reg.by_name("redis_default");
  REQUIRE(redis != nullptr);
  REQUIRE(redis->endpoint_id == "ep_0001");
  REQUIRE(redis->kind == EndpointKind::Redis);
  REQUIRE(redis->static_resolver.host == "127.0.0.1");
  REQUIRE(redis->static_resolver.port == 6379);

  // Check http_api endpoint
  const auto* http = reg.by_name("http_api");
  REQUIRE(http != nullptr);
  REQUIRE(http->endpoint_id == "ep_0002");
  REQUIRE(http->kind == EndpointKind::Http);
}
