#!/usr/bin/env python3
"""Validate writes_eval in --print-plan-info output for reels_plan_a."""
import json
import sys

with open(sys.argv[1]) as f:
    data = json.load(f)

# Must have nodes array
assert "nodes" in data, "Missing nodes array"
nodes = data["nodes"]
assert len(nodes) == 6, f"Expected 6 nodes, got {len(nodes)}"

# Check each node has writes_eval
for node in nodes:
    node_id = node["node_id"]
    assert "writes_eval" in node, f"Node {node_id} missing writes_eval"
    we = node["writes_eval"]
    assert "kind" in we, "writes_eval missing kind"
    assert "keys" in we, "writes_eval missing keys"
    assert we["kind"] in ["Exact", "May", "Unknown"], f"Invalid kind: {we['kind']}"

# Check specific nodes (ops are namespaced: core::follow, core::vm, etc.)
follow_node = next(n for n in nodes if n["op"] == "core::follow")
assert follow_node["writes_eval"]["kind"] == "Exact"
# follow task writes country (3001) from hydration
assert follow_node["writes_eval"]["keys"] == [3001]

# Both vm nodes write to final_score (2001)
vm_nodes = [n for n in nodes if n["op"] == "core::vm"]
assert len(vm_nodes) == 2, f"Expected 2 vm nodes, got {len(vm_nodes)}"
for vm_node in vm_nodes:
    assert vm_node["writes_eval"]["kind"] == "Exact"
    assert vm_node["writes_eval"]["keys"] == [2001]  # final_score

filter_node = next(n for n in nodes if n["op"] == "core::filter")
assert filter_node["writes_eval"]["kind"] == "Exact"
assert filter_node["writes_eval"]["keys"] == []  # no writes

print("writes_eval validation passed")
