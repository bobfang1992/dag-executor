#include "plan.h"
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
      "const_bool", "and", "or", "not", "cmp", "in", "is_null", "not_null"};

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

    // Parse list - Step 06 only supports numeric literals
    for (const auto &item : j["list"]) {
      if (!item.is_number()) {
        throw std::runtime_error(
            "in list contains non-numeric literal (only numbers supported)");
      }
      node->in_list.push_back(item.get<double>());
    }
  } else if (node->op == "is_null" || node->op == "not_null") {
    if (!j.contains("x")) {
      throw std::runtime_error(node->op + " missing 'x'");
    }
    node->value_a = parse_expr_node(j["x"]);
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

  return plan;
}

} // namespace rankd
