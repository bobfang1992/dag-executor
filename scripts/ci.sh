#!/usr/bin/env bash
set -euo pipefail

REPO_ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$REPO_ROOT"

echo "=== Building engine ==="
cmake -S engine -B engine/build -DCMAKE_BUILD_TYPE=Release
cmake --build engine/build --parallel

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
echo "=== All CI tests passed ==="
