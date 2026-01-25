#!/usr/bin/env python3
"""Validate AST expression extraction in compiled plan artifact."""
import json
import sys

with open(sys.argv[1]) as f:
    plan = json.load(f)

# Must have expr_table with at least one entry
assert "expr_table" in plan, "Missing expr_table"
assert len(plan["expr_table"]) >= 1, "expr_table should have at least 1 expression"

# Find the vm node (namespaced as core::vm)
vm_node = next((n for n in plan["nodes"] if n["op"] == "core::vm"), None)
assert vm_node is not None, "Missing vm node"

# vm node should have expr_id pointing to expr_table entry
expr_id = vm_node["params"]["expr_id"]
assert expr_id.startswith("e"), f"expr_id should start with e, got {expr_id}"
assert expr_id in plan["expr_table"], f"expr_id {expr_id} not found in expr_table"

# Verify expression structure: Key.id * coalesce(P.weight, 0.2)
expr = plan["expr_table"][expr_id]
assert expr["op"] == "mul", f"Expected mul op, got {expr['op']}"
assert expr["a"]["op"] == "key_ref", "Left operand should be key_ref"
assert expr["a"]["key_id"] == 1, "Left operand should be Key.id (id=1)"
assert expr["b"]["op"] == "coalesce", "Right operand should be coalesce"
assert expr["b"]["a"]["op"] == "param_ref", "coalesce first arg should be param_ref"
assert expr["b"]["b"]["op"] == "const_number", "coalesce second arg should be const"
assert expr["b"]["b"]["value"] == 0.2, "coalesce fallback should be 0.2"

print("Natural expression compilation validated")
