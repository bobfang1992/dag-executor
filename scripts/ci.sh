#!/usr/bin/env bash
set -euo pipefail

REPO_ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$REPO_ROOT"

echo "=== Building engine ==="
cmake -S engine -B engine/build -DCMAKE_BUILD_TYPE=Release
cmake --build engine/build --parallel

echo ""
echo "=== Running smoke test ==="
REQUEST='{"request_id": "test-123", "plan": "dummy"}'
RESPONSE=$(echo "$REQUEST" | engine/bin/rankd)

echo "Request:  $REQUEST"
echo "Response: $RESPONSE"

# Verify response structure
if ! echo "$RESPONSE" | python3 -c "
import sys, json
r = json.load(sys.stdin)
assert r['request_id'] == 'test-123', 'request_id mismatch'
assert 'engine_request_id' in r, 'missing engine_request_id'
assert len(r['candidates']) == 5, f'expected 5 candidates, got {len(r[\"candidates\"])}'
for i, c in enumerate(r['candidates'], 1):
    assert c['id'] == i, f'candidate {i} has wrong id'
    assert c['fields'] == {}, f'candidate {i} fields not empty'
print('All assertions passed!')
"; then
    echo "FAIL: Response validation failed"
    exit 1
fi

echo ""
echo "=== CI passed ==="
