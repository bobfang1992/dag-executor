#!/usr/bin/env python3
"""Validate mixed expression styles (natural + builder) in compiled plan."""
import json
import sys

with open(sys.argv[1]) as f:
    plan = json.load(f)

# Must have expr_table with at least 2 entries (natural + builder)
assert "expr_table" in plan, "Missing expr_table"
assert len(plan["expr_table"]) >= 2, f"Expected at least 2 expressions, got {len(plan['expr_table'])}"

# Find both vm nodes (namespaced as core::vm)
vm_nodes = [n for n in plan["nodes"] if n["op"] == "core::vm"]
assert len(vm_nodes) == 2, f"Expected 2 vm nodes, got {len(vm_nodes)}"

# Both should have valid expr_ids pointing to expr_table
expr_ids = [n["params"]["expr_id"] for n in vm_nodes]
for eid in expr_ids:
    assert eid in plan["expr_table"], f"expr_id {eid} not in expr_table"

# Verify both expressions are valid
for eid in expr_ids:
    expr = plan["expr_table"][eid]
    assert "op" in expr, f"Expression {eid} missing op"

print("Mixed expression styles validated: both natural and builder-style work together")
