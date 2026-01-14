#pragma once

#include <memory>
#include <nlohmann/json.hpp>
#include <string>
#include <unordered_map>
#include <variant>
#include <vector>

namespace rankd {

struct Node {
  std::string node_id;
  std::string op;
  std::vector<std::string> inputs;
  nlohmann::json params;
};

// ExprNode: recursive expression tree (for vm expressions)
struct ExprNode;
using ExprNodePtr = std::shared_ptr<ExprNode>;

struct ExprNode {
  std::string op; // const_number, const_null, key_ref, param_ref, add, sub, mul,
                  // neg, coalesce

  // For const_number
  double const_value = 0.0;

  // For key_ref
  uint32_t key_id = 0;

  // For param_ref
  uint32_t param_id = 0;

  // For binary ops (add, sub, mul, coalesce): a, b
  // For unary ops (neg): x stored in 'a'
  ExprNodePtr a;
  ExprNodePtr b;
};

// Parse ExprNode from JSON. Throws on invalid structure.
ExprNodePtr parse_expr_node(const nlohmann::json &j);

// PredNode: recursive predicate tree (for filter predicates)
struct PredNode;
using PredNodePtr = std::shared_ptr<PredNode>;

struct PredNode {
  std::string op; // const_bool, and, or, not, cmp, in, is_null, not_null

  // For const_bool
  bool const_value = false;

  // For cmp: comparison operator
  std::string cmp_op; // ==, !=, <, <=, >, >=

  // For cmp, is_null, not_null: operand as ExprNode (value expression)
  ExprNodePtr value_a;
  ExprNodePtr value_b;

  // For and, or: binary predicate operands
  // For not: unary predicate operand stored in pred_a
  PredNodePtr pred_a;
  PredNodePtr pred_b;

  // For in: list of literals (either all numbers or all strings)
  std::vector<double> in_list;        // numeric literals
  std::vector<std::string> in_list_str; // string literals (for Key.country.in(["US","CA"]))
};

// Parse PredNode from JSON. Throws on invalid structure.
PredNodePtr parse_pred_node(const nlohmann::json &j);

struct Plan {
  int schema_version = 0;
  std::string plan_name;
  std::vector<Node> nodes;
  std::vector<std::string> outputs;

  // expr_table: expr_id -> ExprNode tree
  std::unordered_map<std::string, ExprNodePtr> expr_table;

  // pred_table: pred_id -> PredNode tree
  std::unordered_map<std::string, PredNodePtr> pred_table;
};

// Parse plan from JSON file. Throws std::runtime_error on parse failure.
Plan parse_plan(const std::string &path);

} // namespace rankd
