#!/usr/bin/env bash
set -euo pipefail

REPO_ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$REPO_ROOT"

echo "=== Installing DSL dependencies ==="
pnpm -C dsl install --frozen-lockfile

echo ""
echo "=== DSL typecheck ==="
pnpm -C dsl run typecheck

echo ""
echo "=== DSL lint ==="
pnpm -C dsl run lint

echo ""
echo "=== DSL codegen check ==="
pnpm -C dsl run gen:check

echo ""
echo "=== Building engine ==="
cmake -S engine -B engine/build -DCMAKE_BUILD_TYPE=Release
cmake --build engine/build --parallel

echo ""
echo "=== Test 0: RowSet unit tests ==="
engine/bin/rowset_tests

echo ""
echo "=== Test 1: Step 00 fallback (no --plan) ==="
REQUEST='{"request_id": "test-123"}'
RESPONSE=$(echo "$REQUEST" | engine/bin/rankd)

echo "Request:  $REQUEST"
echo "Response: $RESPONSE"

echo "$RESPONSE" | python3 -c "
import sys, json
r = json.load(sys.stdin)
assert r['request_id'] == 'test-123', 'request_id mismatch'
assert 'engine_request_id' in r, 'missing engine_request_id'
assert len(r['candidates']) == 5, f'expected 5 candidates, got {len(r[\"candidates\"])}'
for i, c in enumerate(r['candidates'], 1):
    assert c['id'] == i, f'candidate {i} has wrong id'
    assert c['fields'] == {}, f'candidate {i} fields not empty'
print('PASS: Step 00 fallback works')
"

echo ""
echo "=== Test 2: Execute demo plan ==="
REQUEST='{"request_id": "demo-test"}'
RESPONSE=$(echo "$REQUEST" | engine/bin/rankd --plan artifacts/plans/demo.plan.json)

echo "Request:  $REQUEST"
echo "Response: $RESPONSE"

echo "$RESPONSE" | python3 -c "
import sys, json
r = json.load(sys.stdin)
assert r['request_id'] == 'demo-test', 'request_id mismatch'
assert 'engine_request_id' in r, 'missing engine_request_id'
assert len(r['candidates']) == 5, f'expected 5 candidates, got {len(r[\"candidates\"])}'
expected_ids = [1, 2, 3, 4, 5]
actual_ids = [c['id'] for c in r['candidates']]
assert actual_ids == expected_ids, f'expected ids {expected_ids}, got {actual_ids}'
print('PASS: Demo plan executed correctly')
"

echo ""
echo "=== Test 3: Reject cycle.plan.json ==="
if echo '{}' | engine/bin/rankd --plan artifacts/plans/cycle.plan.json 2>/dev/null; then
    echo "FAIL: cycle.plan.json should have been rejected"
    exit 1
else
    echo "PASS: cycle.plan.json rejected as expected"
fi

echo ""
echo "=== Test 4: Reject missing_input.plan.json ==="
if echo '{}' | engine/bin/rankd --plan artifacts/plans/missing_input.plan.json 2>/dev/null; then
    echo "FAIL: missing_input.plan.json should have been rejected"
    exit 1
else
    echo "PASS: missing_input.plan.json rejected as expected"
fi

echo ""
echo "=== Test 5: Print registry ==="
REGISTRY=$(engine/bin/rankd --print-registry)
echo "Registry: $REGISTRY"

echo "$REGISTRY" | python3 -c "
import sys, json
r = json.load(sys.stdin)
assert 'key_registry_digest' in r, 'missing key_registry_digest'
assert 'param_registry_digest' in r, 'missing param_registry_digest'
assert 'feature_registry_digest' in r, 'missing feature_registry_digest'
assert 'task_manifest_digest' in r, 'missing task_manifest_digest'
assert r['num_keys'] == 8, f'expected 8 keys, got {r[\"num_keys\"]}'
assert r['num_params'] == 3, f'expected 3 params, got {r[\"num_params\"]}'
assert r['num_features'] == 2, f'expected 2 features, got {r[\"num_features\"]}'
assert r['num_tasks'] == 2, f'expected 2 tasks, got {r[\"num_tasks\"]}'
print('PASS: Registry info correct')
"

echo ""
echo "=== Test 6: Reject bad_type_fanout.plan.json (string instead of int) ==="
if echo '{}' | engine/bin/rankd --plan artifacts/plans/bad_type_fanout.plan.json 2>/dev/null; then
    echo "FAIL: bad_type_fanout.plan.json should have been rejected"
    exit 1
else
    echo "PASS: bad_type_fanout.plan.json rejected as expected"
fi

echo ""
echo "=== Test 7: Reject missing_fanout.plan.json (missing required param) ==="
if echo '{}' | engine/bin/rankd --plan artifacts/plans/missing_fanout.plan.json 2>/dev/null; then
    echo "FAIL: missing_fanout.plan.json should have been rejected"
    exit 1
else
    echo "PASS: missing_fanout.plan.json rejected as expected"
fi

echo ""
echo "=== Test 8: Reject extra_param.plan.json (unknown param) ==="
if echo '{}' | engine/bin/rankd --plan artifacts/plans/extra_param.plan.json 2>/dev/null; then
    echo "FAIL: extra_param.plan.json should have been rejected"
    exit 1
else
    echo "PASS: extra_param.plan.json rejected as expected"
fi

echo ""
echo "=== Test 9: Reject bad_trace_type.plan.json (int instead of string) ==="
if echo '{}' | engine/bin/rankd --plan artifacts/plans/bad_trace_type.plan.json 2>/dev/null; then
    echo "FAIL: bad_trace_type.plan.json should have been rejected"
    exit 1
else
    echo "PASS: bad_trace_type.plan.json rejected as expected"
fi

echo ""
echo "=== Test 10: Accept null_trace.plan.json (null for nullable param) ==="
RESPONSE=$(echo '{"request_id": "null-trace-test"}' | engine/bin/rankd --plan artifacts/plans/null_trace.plan.json 2>&1)
if echo "$RESPONSE" | grep -q '"candidates"'; then
    echo "PASS: null_trace.plan.json accepted as expected"
else
    echo "FAIL: null_trace.plan.json should have been accepted"
    echo "Response: $RESPONSE"
    exit 1
fi

echo ""
echo "=== Test 11: Reject large_fanout.plan.json (exceeds 10M limit) ==="
if echo '{}' | engine/bin/rankd --plan artifacts/plans/large_fanout.plan.json 2>/dev/null; then
    echo "FAIL: large_fanout.plan.json should have been rejected"
    exit 1
else
    echo "PASS: large_fanout.plan.json rejected as expected"
fi

echo ""
echo "=== All CI tests passed ==="
