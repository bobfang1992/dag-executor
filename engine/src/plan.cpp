#include "plan.h"
#include "capability_registry.h"
#include <algorithm>
#include <fstream>
#include <stdexcept>
#include <unordered_set>

namespace rankd {

ExprNodePtr parse_expr_node(const nlohmann::json &j) {
  if (!j.is_object()) {
    throw std::runtime_error("ExprNode must be an object");
  }
  if (!j.contains("op") || !j["op"].is_string()) {
    throw std::runtime_error("ExprNode missing or invalid 'op'");
  }

  auto node = std::make_shared<ExprNode>();
  node->op = j["op"].get<std::string>();

  static const std::unordered_set<std::string> valid_ops = {
      "const_number", "const_null", "key_ref", "param_ref",
      "add",          "sub",        "mul",     "neg",
      "coalesce"};

  if (valid_ops.find(node->op) == valid_ops.end()) {
    throw std::runtime_error("Unknown ExprNode op: " + node->op);
  }

  if (node->op == "const_number") {
    if (!j.contains("value") || !j["value"].is_number()) {
      throw std::runtime_error("const_number missing or invalid 'value'");
    }
    node->const_value = j["value"].get<double>();
  } else if (node->op == "const_null") {
    // No additional fields
  } else if (node->op == "key_ref") {
    if (!j.contains("key_id") || !j["key_id"].is_number_unsigned()) {
      throw std::runtime_error("key_ref missing or invalid 'key_id'");
    }
    node->key_id = j["key_id"].get<uint32_t>();
  } else if (node->op == "param_ref") {
    if (!j.contains("param_id") || !j["param_id"].is_number_unsigned()) {
      throw std::runtime_error("param_ref missing or invalid 'param_id'");
    }
    node->param_id = j["param_id"].get<uint32_t>();
  } else if (node->op == "add" || node->op == "sub" || node->op == "mul" ||
             node->op == "coalesce") {
    if (!j.contains("a")) {
      throw std::runtime_error(node->op + " missing 'a'");
    }
    if (!j.contains("b")) {
      throw std::runtime_error(node->op + " missing 'b'");
    }
    node->a = parse_expr_node(j["a"]);
    node->b = parse_expr_node(j["b"]);
  } else if (node->op == "neg") {
    if (!j.contains("x")) {
      throw std::runtime_error("neg missing 'x'");
    }
    node->a = parse_expr_node(j["x"]);
  }

  return node;
}

PredNodePtr parse_pred_node(const nlohmann::json &j) {
  if (!j.is_object()) {
    throw std::runtime_error("PredNode must be an object");
  }
  if (!j.contains("op") || !j["op"].is_string()) {
    throw std::runtime_error("PredNode missing or invalid 'op'");
  }

  auto node = std::make_shared<PredNode>();
  node->op = j["op"].get<std::string>();

  static const std::unordered_set<std::string> valid_ops = {
      "const_bool", "and", "or",  "not",    "cmp",
      "in",         "is_null", "not_null", "regex"};

  if (valid_ops.find(node->op) == valid_ops.end()) {
    throw std::runtime_error("Unknown PredNode op: " + node->op);
  }

  if (node->op == "const_bool") {
    if (!j.contains("value") || !j["value"].is_boolean()) {
      throw std::runtime_error("const_bool missing or invalid 'value'");
    }
    node->const_value = j["value"].get<bool>();
  } else if (node->op == "and" || node->op == "or") {
    if (!j.contains("a")) {
      throw std::runtime_error(node->op + " missing 'a'");
    }
    if (!j.contains("b")) {
      throw std::runtime_error(node->op + " missing 'b'");
    }
    node->pred_a = parse_pred_node(j["a"]);
    node->pred_b = parse_pred_node(j["b"]);
  } else if (node->op == "not") {
    if (!j.contains("x")) {
      throw std::runtime_error("not missing 'x'");
    }
    node->pred_a = parse_pred_node(j["x"]);
  } else if (node->op == "cmp") {
    if (!j.contains("cmp") || !j["cmp"].is_string()) {
      throw std::runtime_error("cmp missing or invalid 'cmp' operator");
    }
    node->cmp_op = j["cmp"].get<std::string>();

    static const std::unordered_set<std::string> valid_cmp_ops = {
        "==", "!=", "<", "<=", ">", ">="};
    if (valid_cmp_ops.find(node->cmp_op) == valid_cmp_ops.end()) {
      throw std::runtime_error("Unknown cmp operator: " + node->cmp_op);
    }

    if (!j.contains("a")) {
      throw std::runtime_error("cmp missing 'a'");
    }
    if (!j.contains("b")) {
      throw std::runtime_error("cmp missing 'b'");
    }
    node->value_a = parse_expr_node(j["a"]);
    node->value_b = parse_expr_node(j["b"]);
  } else if (node->op == "in") {
    if (!j.contains("lhs")) {
      throw std::runtime_error("in missing 'lhs'");
    }
    if (!j.contains("list") || !j["list"].is_array()) {
      throw std::runtime_error("in missing or invalid 'list'");
    }
    node->value_a = parse_expr_node(j["lhs"]);

    // Parse list - determine type from first element (all elements must match)
    const auto &list = j["list"];
    if (list.empty()) {
      // Empty list is fine (always evaluates to false)
    } else if (list[0].is_number()) {
      // Numeric list
      for (const auto &item : list) {
        if (!item.is_number()) {
          throw std::runtime_error(
              "in list contains mixed types (expected all numbers)");
        }
        node->in_list.push_back(item.get<double>());
      }
    } else if (list[0].is_string()) {
      // String list (e.g., Key.country.in(["US","CA"]))
      for (const auto &item : list) {
        if (!item.is_string()) {
          throw std::runtime_error(
              "in list contains mixed types (expected all strings)");
        }
        node->in_list_str.push_back(item.get<std::string>());
      }
    } else {
      throw std::runtime_error(
          "in list contains unsupported type (expected numbers or strings)");
    }
  } else if (node->op == "is_null" || node->op == "not_null") {
    if (!j.contains("x")) {
      throw std::runtime_error(node->op + " missing 'x'");
    }
    node->value_a = parse_expr_node(j["x"]);
  } else if (node->op == "regex") {
    // Required: key_id (must reference string column)
    if (!j.contains("key_id") || !j["key_id"].is_number_unsigned()) {
      throw std::runtime_error("regex missing or invalid 'key_id'");
    }
    node->regex_key_id = j["key_id"].get<uint32_t>();

    // Required: pattern object with kind
    if (!j.contains("pattern") || !j["pattern"].is_object()) {
      throw std::runtime_error("regex missing or invalid 'pattern'");
    }
    const auto &pat = j["pattern"];
    if (!pat.contains("kind") || !pat["kind"].is_string()) {
      throw std::runtime_error("regex pattern missing 'kind'");
    }
    std::string kind = pat["kind"].get<std::string>();

    if (kind == "literal") {
      if (!pat.contains("value") || !pat["value"].is_string()) {
        throw std::runtime_error("regex literal pattern missing 'value'");
      }
      node->regex_pattern = pat["value"].get<std::string>();
      node->regex_param_id = 0;
    } else if (kind == "param") {
      if (!pat.contains("param_id") || !pat["param_id"].is_number_unsigned()) {
        throw std::runtime_error("regex param pattern missing 'param_id'");
      }
      node->regex_param_id = pat["param_id"].get<uint32_t>();
    } else {
      throw std::runtime_error("regex pattern kind must be 'literal' or 'param'");
    }

    // Optional: flags (default "")
    node->regex_flags = "";
    if (j.contains("flags")) {
      if (!j["flags"].is_string()) {
        throw std::runtime_error("regex 'flags' must be string");
      }
      node->regex_flags = j["flags"].get<std::string>();
      if (node->regex_flags != "" && node->regex_flags != "i") {
        throw std::runtime_error("regex 'flags' must be '' or 'i'");
      }
    }
  }

  return node;
}

Plan parse_plan(const std::string &path) {
  std::ifstream file(path);
  if (!file) {
    throw std::runtime_error("Cannot open plan file: " + path);
  }

  nlohmann::json j;
  try {
    j = nlohmann::json::parse(file);
  } catch (const nlohmann::json::parse_error &e) {
    throw std::runtime_error("Invalid JSON in plan file: " +
                             std::string(e.what()));
  }

  Plan plan;

  // schema_version
  if (!j.contains("schema_version") ||
      !j["schema_version"].is_number_integer()) {
    throw std::runtime_error("Plan missing or invalid 'schema_version'");
  }
  plan.schema_version = j["schema_version"].get<int>();

  // plan_name
  if (!j.contains("plan_name") || !j["plan_name"].is_string()) {
    throw std::runtime_error("Plan missing or invalid 'plan_name'");
  }
  plan.plan_name = j["plan_name"].get<std::string>();

  // nodes
  if (!j.contains("nodes") || !j["nodes"].is_array()) {
    throw std::runtime_error("Plan missing or invalid 'nodes'");
  }
  for (const auto &nj : j["nodes"]) {
    Node node;
    if (!nj.contains("node_id") || !nj["node_id"].is_string()) {
      throw std::runtime_error("Node missing or invalid 'node_id'");
    }
    node.node_id = nj["node_id"].get<std::string>();

    if (!nj.contains("op") || !nj["op"].is_string()) {
      throw std::runtime_error("Node '" + node.node_id +
                               "' missing or invalid 'op'");
    }
    node.op = nj["op"].get<std::string>();

    if (!nj.contains("inputs") || !nj["inputs"].is_array()) {
      throw std::runtime_error("Node '" + node.node_id +
                               "' missing or invalid 'inputs'");
    }
    for (const auto &inp : nj["inputs"]) {
      if (!inp.is_string()) {
        throw std::runtime_error("Node '" + node.node_id +
                                 "' has non-string input");
      }
      node.inputs.push_back(inp.get<std::string>());
    }

    if (nj.contains("params")) {
      node.params = nj["params"];
    } else {
      node.params = nlohmann::json::object();
    }

    plan.nodes.push_back(std::move(node));
  }

  // outputs
  if (!j.contains("outputs") || !j["outputs"].is_array()) {
    throw std::runtime_error("Plan missing or invalid 'outputs'");
  }
  for (const auto &out : j["outputs"]) {
    if (!out.is_string()) {
      throw std::runtime_error("Plan has non-string output");
    }
    plan.outputs.push_back(out.get<std::string>());
  }

  // expr_table (optional)
  if (j.contains("expr_table")) {
    if (!j["expr_table"].is_object()) {
      throw std::runtime_error("Plan 'expr_table' must be an object");
    }
    for (auto it = j["expr_table"].begin(); it != j["expr_table"].end(); ++it) {
      const std::string &expr_id = it.key();
      try {
        plan.expr_table[expr_id] = parse_expr_node(it.value());
      } catch (const std::runtime_error &e) {
        throw std::runtime_error("Error parsing expr '" + expr_id +
                                 "': " + e.what());
      }
    }
  }

  // pred_table (optional)
  if (j.contains("pred_table")) {
    if (!j["pred_table"].is_object()) {
      throw std::runtime_error("Plan 'pred_table' must be an object");
    }
    for (auto it = j["pred_table"].begin(); it != j["pred_table"].end(); ++it) {
      const std::string &pred_id = it.key();
      try {
        plan.pred_table[pred_id] = parse_pred_node(it.value());
      } catch (const std::runtime_error &e) {
        throw std::runtime_error("Error parsing pred '" + pred_id +
                                 "': " + e.what());
      }
    }
  }

  // RFC0001: capabilities_required (optional)
  if (j.contains("capabilities_required")) {
    if (!j["capabilities_required"].is_array()) {
      throw std::runtime_error("Plan 'capabilities_required' must be an array");
    }
    for (const auto &cap : j["capabilities_required"]) {
      if (!cap.is_string()) {
        throw std::runtime_error(
            "Plan 'capabilities_required' contains non-string element");
      }
      plan.capabilities_required.push_back(cap.get<std::string>());
    }
    // Validate sorted + unique (don't re-sort, reject if not canonical)
    if (!std::is_sorted(plan.capabilities_required.begin(),
                        plan.capabilities_required.end())) {
      throw std::runtime_error(
          "Plan 'capabilities_required' must be sorted lexicographically");
    }
    auto it = std::adjacent_find(plan.capabilities_required.begin(),
                                 plan.capabilities_required.end());
    if (it != plan.capabilities_required.end()) {
      throw std::runtime_error(
          "Plan 'capabilities_required' contains duplicate: " + *it);
    }
  }

  // Build set of required capabilities for validation
  std::unordered_set<std::string> caps_set(plan.capabilities_required.begin(),
                                           plan.capabilities_required.end());

  // RFC0001: extensions (optional)
  if (j.contains("extensions")) {
    if (!j["extensions"].is_object()) {
      throw std::runtime_error("Plan 'extensions' must be an object");
    }
    plan.extensions = j["extensions"];
    // Validate every key is in capabilities_required
    for (auto it = plan.extensions.begin(); it != plan.extensions.end(); ++it) {
      const std::string &key = it.key();
      if (caps_set.find(key) == caps_set.end()) {
        throw std::runtime_error(
            "Plan extension key '" + key +
            "' not in capabilities_required");
      }
      // Validate payload
      validate_capability_payload(key, it.value(), "plan");
    }
  }

  // Ensure capabilities with required fields have extensions entries
  for (const auto &cap_id : plan.capabilities_required) {
    if (capability_has_required_fields(cap_id)) {
      if (!plan.extensions.contains(cap_id)) {
        throw std::runtime_error("capability '" + cap_id +
                                 "' has required fields but no extensions entry");
      }
    }
  }

  // RFC0001: node.extensions (already parsed above, now validate)
  for (size_t i = 0; i < plan.nodes.size(); ++i) {
    auto &node = plan.nodes[i];
    const auto &nj = j["nodes"][i];
    if (nj.contains("extensions")) {
      if (!nj["extensions"].is_object()) {
        throw std::runtime_error("Node '" + node.node_id +
                                 "' extensions must be an object");
      }
      node.extensions = nj["extensions"];
      // Validate every key is in plan.capabilities_required
      for (auto it = node.extensions.begin(); it != node.extensions.end();
           ++it) {
        const std::string &key = it.key();
        if (caps_set.find(key) == caps_set.end()) {
          throw std::runtime_error("Node '" + node.node_id +
                                   "' extension key '" + key +
                                   "' requires plan capability '" + key + "'");
        }
        // Validate payload
        validate_capability_payload(key, it.value(), "node:" + node.node_id);
      }
    }
  }

  return plan;
}

} // namespace rankd
