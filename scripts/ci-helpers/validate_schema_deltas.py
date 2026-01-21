#!/usr/bin/env python3
"""Validate schema_deltas in --dump-run-trace output."""
import json
import sys

with open(sys.argv[1]) as f:
    data = json.load(f)

# Must have schema_deltas array
assert "schema_deltas" in data, "Missing schema_deltas array"
deltas = data["schema_deltas"]
assert len(deltas) == 5, f"Expected 5 schema deltas, got {len(deltas)}"

# Check each delta has required fields
for d in deltas:
    assert "node_id" in d, "Missing node_id"
    assert "in_keys_union" in d, "Missing in_keys_union"
    assert "out_keys" in d, "Missing out_keys"
    assert "new_keys" in d, "Missing new_keys"
    assert "removed_keys" in d, "Missing removed_keys"

# Find viewer source (n0) - should have new key for id (1)
viewer_delta = next(d for d in deltas if d["node_id"] == "n0")
assert 1 in viewer_delta["new_keys"], "viewer should add id (1)"

# Find follow node (n1) - should have new key for country (3001)
follow_delta = next(d for d in deltas if d["node_id"] == "n1")
assert 3001 in follow_delta["new_keys"], "follow should add country (3001)"

# Find vm node (n2) - should have new key for final_score (2001)
vm_delta = next(d for d in deltas if d["node_id"] == "n2")
assert vm_delta["new_keys"] == [2001], "vm should add final_score (2001)"

# Find filter node (n3) - should have empty new_keys (row-only op)
filter_delta = next(d for d in deltas if d["node_id"] == "n3")
assert filter_delta["new_keys"] == [], "filter should not add columns"
assert filter_delta["removed_keys"] == [], "filter should not remove columns"

# Find take node (n4) - should have empty new_keys (row-only op)
take_delta = next(d for d in deltas if d["node_id"] == "n4")
assert take_delta["new_keys"] == [], "take should not add columns"
assert take_delta["removed_keys"] == [], "take should not remove columns"

print("schema_deltas validation passed")
