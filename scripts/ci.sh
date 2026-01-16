#!/usr/bin/env bash
set -euo pipefail

REPO_ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$REPO_ROOT"

# Create temp directory for parallel job outputs
# Note: Don't use TMPDIR as variable name - it's a system env var on macOS
CI_TEMP=$(mktemp -d)
trap "rm -rf $CI_TEMP" EXIT

# Track background job PIDs and their descriptions
declare -a PIDS=()
declare -a DESCRIPTIONS=()

# Run a command in background, capturing output
run_bg() {
    local desc="$1"
    local outfile="$CI_TEMP/${#PIDS[@]}.out"
    shift
    "$@" > "$outfile" 2>&1 &
    PIDS+=($!)
    DESCRIPTIONS+=("$desc")
}

# Wait for all background jobs and report results
wait_all() {
    local failed=0
    for i in "${!PIDS[@]}"; do
        if wait "${PIDS[$i]}"; then
            echo "✓ ${DESCRIPTIONS[$i]}"
        else
            echo "✗ ${DESCRIPTIONS[$i]} FAILED"
            echo "--- Output ---"
            cat "$CI_TEMP/$i.out"
            echo "--- End ---"
            failed=1
        fi
    done
    PIDS=()
    DESCRIPTIONS=()
    if [ $failed -eq 1 ]; then
        exit 1
    fi
}

echo "=== Phase 1: Install dependencies ==="
pnpm install --frozen-lockfile

echo ""
echo "=== Phase 2: Codegen check ==="
pnpm -C dsl run gen:check

echo ""
echo "=== Phase 3: Build DSL + Engine (parallel) ==="
run_bg "Build DSL + compile plans" pnpm run build
run_bg "Build engine" bash -c "cmake -S engine -B engine/build -DCMAKE_BUILD_TYPE=Release -DCMAKE_EXPORT_COMPILE_COMMANDS=ON && cmake --build engine/build --parallel"
wait_all

echo ""
echo "=== Phase 4: Checks + Unit tests (parallel) ==="
run_bg "DSL typecheck" pnpm -C dsl run typecheck
run_bg "DSL lint" pnpm -C dsl run lint
run_bg "Unit tests (rankd)" engine/bin/rankd_tests
run_bg "Unit tests (concat)" engine/bin/concat_tests
run_bg "Unit tests (regex)" engine/bin/regex_tests
wait_all

echo ""
echo "=== Phase 5: Integration tests ==="

# Helper to run a test and check result
run_test() {
    local name="$1"
    local outfile="$CI_TEMP/test_${name//[^a-zA-Z0-9]/_}.out"
    shift
    if "$@" > "$outfile" 2>&1; then
        echo "✓ $name"
        return 0
    else
        echo "✗ $name FAILED"
        cat "$outfile"
        return 1
    fi
}

# Test runner for engine tests (these are fast, run in batches)
test_engine() {
    local name="$1"
    local request="$2"
    local plan_arg="$3"
    local validator="$4"

    local response
    if [ -n "$plan_arg" ]; then
        response=$(echo "$request" | engine/bin/rankd $plan_arg 2>&1)
    else
        response=$(echo "$request" | engine/bin/rankd 2>&1)
    fi

    echo "$response" | python3 -c "$validator"
}

# Test runner for rejection tests
test_reject() {
    local plan="$1"
    if echo '{}' | engine/bin/rankd --plan "$plan" 2>/dev/null; then
        return 1
    fi
    return 0
}

# Test runner for error message tests
test_error_msg() {
    local request="$1"
    local plan="$2"
    local expected_error="$3"

    local response
    response=$(echo "$request" | engine/bin/rankd --plan "$plan" 2>&1 || true)
    if echo "$response" | grep -q '"error"' && echo "$response" | grep -q "$expected_error"; then
        return 0
    fi
    echo "Response: $response"
    echo "Expected error containing: $expected_error"
    return 1
}

# Batch 1: Basic engine tests (parallel)
echo "--- Batch 1: Basic engine tests ---"
run_bg "Test 1: Step 00 fallback" bash -c '
response=$(echo "{\"request_id\": \"test-123\"}" | engine/bin/rankd)
echo "$response" | python3 -c "
import sys, json
r = json.load(sys.stdin)
assert r[\"request_id\"] == \"test-123\"
assert \"engine_request_id\" in r
assert len(r[\"candidates\"]) == 5
for i, c in enumerate(r[\"candidates\"], 1):
    assert c[\"id\"] == i
    assert c[\"fields\"] == {}
"'

run_bg "Test 2: Demo plan" bash -c '
response=$(echo "{\"request_id\": \"demo-test\"}" | engine/bin/rankd --plan artifacts/plans/demo.plan.json)
echo "$response" | python3 -c "
import sys, json
r = json.load(sys.stdin)
assert r[\"request_id\"] == \"demo-test\"
assert len(r[\"candidates\"]) == 5
expected_ids = [3, 4, 5, 6, 7]
actual_ids = [c[\"id\"] for c in r[\"candidates\"]]
assert actual_ids == expected_ids
"'

run_bg "Test 3: Reject cycle.plan.json" bash -c '
if echo "{}" | engine/bin/rankd --plan artifacts/plans/cycle.plan.json 2>/dev/null; then exit 1; fi
'

run_bg "Test 4: Reject missing_input.plan.json" bash -c '
if echo "{}" | engine/bin/rankd --plan artifacts/plans/missing_input.plan.json 2>/dev/null; then exit 1; fi
'

run_bg "Test 5: Print registry" bash -c '
engine/bin/rankd --print-registry | python3 -c "
import sys, json
r = json.load(sys.stdin)
assert \"key_registry_digest\" in r
assert \"param_registry_digest\" in r
assert \"feature_registry_digest\" in r
assert \"task_manifest_digest\" in r
assert r[\"num_keys\"] == 8
assert r[\"num_params\"] == 3
assert r[\"num_features\"] == 2
assert r[\"num_tasks\"] == 6
"'
wait_all

# Batch 2: Param validation tests (parallel)
echo "--- Batch 2: Param validation tests ---"
run_bg "Test 6: Reject bad_type_fanout" bash -c '
if echo "{}" | engine/bin/rankd --plan artifacts/plans/bad_type_fanout.plan.json 2>/dev/null; then exit 1; fi
'
run_bg "Test 7: Reject missing_fanout" bash -c '
if echo "{}" | engine/bin/rankd --plan artifacts/plans/missing_fanout.plan.json 2>/dev/null; then exit 1; fi
'
run_bg "Test 8: Reject extra_param" bash -c '
if echo "{}" | engine/bin/rankd --plan artifacts/plans/extra_param.plan.json 2>/dev/null; then exit 1; fi
'
run_bg "Test 9: Reject bad_trace_type" bash -c '
if echo "{}" | engine/bin/rankd --plan artifacts/plans/bad_trace_type.plan.json 2>/dev/null; then exit 1; fi
'
run_bg "Test 10: Accept null_trace" bash -c '
response=$(echo "{\"request_id\": \"null-trace\"}" | engine/bin/rankd --plan artifacts/plans/null_trace.plan.json 2>&1)
echo "$response" | grep -q "\"candidates\""
'
run_bg "Test 11: Reject large_fanout" bash -c '
if echo "{}" | engine/bin/rankd --plan artifacts/plans/large_fanout.plan.json 2>/dev/null; then exit 1; fi
'
wait_all

# Batch 3: param_overrides tests (parallel)
echo "--- Batch 3: param_overrides tests ---"
run_bg "Test 12: Valid param_overrides" bash -c '
response=$(echo "{\"request_id\": \"p\", \"param_overrides\": {\"media_age_penalty_weight\": 0.5, \"esr_cutoff\": 2.0}}" | engine/bin/rankd --plan artifacts/plans/demo.plan.json)
echo "$response" | python3 -c "
import sys, json
r = json.load(sys.stdin)
assert [c[\"id\"] for c in r[\"candidates\"]] == [2, 3, 4, 5, 6]
"'
run_bg "Test 13: Reject unknown param" bash -c '
response=$(echo "{\"request_id\": \"x\", \"param_overrides\": {\"unknown_param\": 42}}" | engine/bin/rankd --plan artifacts/plans/demo.plan.json 2>&1 || true)
echo "$response" | grep -q "unknown param"
'
run_bg "Test 14: Reject wrong type" bash -c '
response=$(echo "{\"request_id\": \"x\", \"param_overrides\": {\"media_age_penalty_weight\": \"bad\"}}" | engine/bin/rankd --plan artifacts/plans/demo.plan.json 2>&1 || true)
echo "$response" | grep -q "must be float"
'
run_bg "Test 15: Reject null non-nullable" bash -c '
response=$(echo "{\"request_id\": \"x\", \"param_overrides\": {\"media_age_penalty_weight\": null}}" | engine/bin/rankd --plan artifacts/plans/demo.plan.json 2>&1 || true)
echo "$response" | grep -q "cannot be null"
'
run_bg "Test 16: Accept null nullable" bash -c '
response=$(echo "{\"request_id\": \"x\", \"param_overrides\": {\"blocklist_regex\": null}}" | engine/bin/rankd --plan artifacts/plans/demo.plan.json)
echo "$response" | python3 -c "
import sys, json
r = json.load(sys.stdin)
assert [c[\"id\"] for c in r[\"candidates\"]] == [3, 4, 5, 6, 7]
"'
wait_all

# Batch 4: Predicate tests (parallel)
echo "--- Batch 4: Predicate tests ---"
run_bg "Test 17: Reject missing_pred_id" bash -c '
if echo "{}" | engine/bin/rankd --plan artifacts/plans/missing_pred_id.plan.json 2>/dev/null; then exit 1; fi
'
run_bg "Test 18: Reject unknown_pred_id" bash -c '
if echo "{}" | engine/bin/rankd --plan artifacts/plans/unknown_pred_id.plan.json 2>/dev/null; then exit 1; fi
'
run_bg "Test 19: Reject bad_pred_table_shape" bash -c '
if echo "{}" | engine/bin/rankd --plan artifacts/plans/bad_pred_table_shape.plan.json 2>/dev/null; then exit 1; fi
'
run_bg "Test 20: Reject bad_in_list" bash -c '
if echo "{}" | engine/bin/rankd --plan artifacts/plans/bad_in_list.plan.json 2>/dev/null; then exit 1; fi
'
run_bg "Test 21: String in-list" bash -c '
response=$(echo "{\"request_id\": \"x\"}" | engine/bin/rankd --plan artifacts/plans/string_in_list.plan.json)
echo "$response" | python3 -c "
import sys, json
r = json.load(sys.stdin)
assert len(r[\"candidates\"]) == 10
assert [c[\"id\"] for c in r[\"candidates\"]] == list(range(1, 11))
"'
wait_all

# Batch 5: Concat and TypeScript plans (parallel)
echo "--- Batch 5: Concat and TS plans ---"
run_bg "Test 22: concat_demo" bash -c '
response=$(echo "{\"request_id\": \"x\"}" | engine/bin/rankd --plan artifacts/plans/concat_demo.plan.json)
echo "$response" | python3 -c "
import sys, json
r = json.load(sys.stdin)
assert [c[\"id\"] for c in r[\"candidates\"]] == [1, 2, 3, 4, 1001, 1002, 1003, 1004]
"'
run_bg "Test 23: Reject concat_bad_arity" bash -c '
if echo "{}" | engine/bin/rankd --plan artifacts/plans/concat_bad_arity.plan.json 2>/dev/null; then exit 1; fi
'
run_bg "Test 24: reels_plan_a" bash -c '
response=$(echo "{\"request_id\": \"x\"}" | engine/bin/rankd --plan artifacts/plans/reels_plan_a.plan.json)
echo "$response" | python3 -c "
import sys, json
r = json.load(sys.stdin)
assert [c[\"id\"] for c in r[\"candidates\"]] == [3, 4, 5, 6, 7]
"'
run_bg "Test 25: reels_plan_a with overrides" bash -c '
response=$(echo "{\"request_id\": \"x\", \"param_overrides\": {\"media_age_penalty_weight\": 0.5}}" | engine/bin/rankd --plan artifacts/plans/reels_plan_a.plan.json)
echo "$response" | python3 -c "
import sys, json
r = json.load(sys.stdin)
assert [c[\"id\"] for c in r[\"candidates\"]] == [2, 3, 4, 5, 6]
"'
run_bg "Test 26: concat_plan" bash -c '
response=$(echo "{\"request_id\": \"x\"}" | engine/bin/rankd --plan artifacts/plans/concat_plan.plan.json)
echo "$response" | python3 -c "
import sys, json
r = json.load(sys.stdin)
assert [c[\"id\"] for c in r[\"candidates\"]] == [1, 2, 3, 4, 1001, 1002, 1003, 1004]
"'
run_bg "Test 27: regex_plan" bash -c '
response=$(echo "{\"request_id\": \"x\"}" | engine/bin/rankd --plan artifacts/plans/regex_plan.plan.json)
echo "$response" | python3 -c "
import sys, json
r = json.load(sys.stdin)
assert [c[\"id\"] for c in r[\"candidates\"]] == [1, 3, 5, 7, 9]
"'
wait_all

# Batch 6: Regex and encapsulation tests (parallel)
echo "--- Batch 6: Regex tests ---"
run_bg "Test 28: No RowSet internals access" bash -c '
if grep -r --include="*.cpp" --include="*.h" -E "\.(selection_|order_)\b" engine/src engine/tests 2>/dev/null | grep -v "rowset.h"; then
    exit 1
fi
'
run_bg "Test 29: regex_demo" bash -c '
response=$(echo "{\"request_id\": \"x\"}" | engine/bin/rankd --plan artifacts/plans/regex_demo.plan.json)
echo "$response" | python3 -c "
import sys, json
r = json.load(sys.stdin)
assert [c[\"id\"] for c in r[\"candidates\"]] == [1, 3, 5, 7, 9]
"'
run_bg "Test 30: regex_param_demo" bash -c '
response=$(echo "{\"request_id\": \"x\", \"param_overrides\": {\"blocklist_regex\": \"CA\"}}" | engine/bin/rankd --plan artifacts/plans/regex_param_demo.plan.json)
echo "$response" | python3 -c "
import sys, json
r = json.load(sys.stdin)
assert [c[\"id\"] for c in r[\"candidates\"]] == [2, 4, 6, 8, 10]
"'
run_bg "Test 31: Reject bad_regex_flags" bash -c '
if echo "{}" | engine/bin/rankd --plan artifacts/plans/bad_regex_flags.plan.json 2>/dev/null; then exit 1; fi
'
wait_all

# Batch 7: QuickJS sandbox + plan store tests (parallel)
echo "--- Batch 7: Sandbox + plan store ---"
run_bg "Test 32: Reject evil.plan.ts" bash -c '
if node dsl/packages/compiler/dist/cli.js build test/fixtures/plans/evil.plan.ts --out /tmp/ci-evil-32 2>/dev/null; then exit 1; fi
'
run_bg "Test 33: Reject evil_proto.plan.ts" bash -c '
if node dsl/packages/compiler/dist/cli.js build test/fixtures/plans/evil_proto.plan.ts --out /tmp/ci-evil-33 2>/dev/null; then exit 1; fi
'
run_bg "Test 34: --plan_name" bash -c '
response=$(echo "{\"request_id\": \"x\"}" | engine/bin/rankd --plan_name reels_plan_a)
echo "$response" | python3 -c "
import sys, json
r = json.load(sys.stdin)
assert [c[\"id\"] for c in r[\"candidates\"]] == [3, 4, 5, 6, 7]
"'
run_bg "Test 35: Reject path traversal" bash -c '
if echo "{}" | engine/bin/rankd --plan_name "../x" 2>/dev/null; then exit 1; fi
'
run_bg "Test 36: Reject slash in name" bash -c '
if echo "{}" | engine/bin/rankd --plan_name "a/b" 2>/dev/null; then exit 1; fi
'
run_bg "Test 37: index.json" bash -c '
python3 -c "
import json
with open(\"artifacts/plans/index.json\") as f:
    idx = json.load(f)
assert idx[\"schema_version\"] == 1
assert len(idx[\"plans\"]) >= 3
names = [p[\"name\"] for p in idx[\"plans\"]]
assert \"reels_plan_a\" in names
assert \"concat_plan\" in names
assert \"regex_plan\" in names
"'
wait_all

# Batch 8: RFC0001 capabilities tests (parallel)
# Use direct CLI invocation to avoid pnpm lock contention
echo "--- Batch 8: RFC0001 capabilities ---"
run_bg "Test 38: Reject name_mismatch" bash -c '
node dsl/packages/compiler/dist/cli.js build test/fixtures/plans/name_mismatch.plan.ts --out /tmp/ci-mismatch 2>&1 | grep -q "doesn'\''t match filename"
'
run_bg "Test 39: Reject bad_caps_unsorted" bash -c '
node dsl/packages/compiler/dist/cli.js build test/fixtures/plans/bad_caps_unsorted.plan.ts --out /tmp/ci-caps 2>&1 | grep -q "must be sorted and unique"
'
run_bg "Test 40: Reject bad_ext_key" bash -c '
node dsl/packages/compiler/dist/cli.js build test/fixtures/plans/bad_ext_key_not_required.plan.ts --out /tmp/ci-ext 2>&1 | grep -q "must appear in capabilities_required"
'
run_bg "Test 41: Reject bad_node_ext" bash -c '
node dsl/packages/compiler/dist/cli.js build test/fixtures/plans/bad_node_ext_not_declared.plan.ts --out /tmp/ci-node 2>&1 | grep -q "requires plan capability"
'
run_bg "Test 42: valid_capabilities" bash -c '
node dsl/packages/compiler/dist/cli.js build test/fixtures/plans/valid_capabilities.plan.ts --out /tmp/ci-valid-caps
python3 -c "
import json
with open(\"/tmp/ci-valid-caps/valid_capabilities.plan.json\") as f:
    plan = json.load(f)
assert plan[\"capabilities_required\"] == [\"cap.audit\", \"cap.debug\"]
assert \"cap.debug\" in plan[\"extensions\"]
assert plan[\"nodes\"][0][\"extensions\"][\"cap.debug\"][\"node_debug\"] == True
"'
wait_all

# Batch 9: Compiler parity tests (parallel)
# Use direct CLI invocation to avoid pnpm lock contention
echo "--- Batch 9: Compiler parity ---"
run_bg "Test 43: Parity (valid_capabilities)" bash -c '
mkdir -p /tmp/parity-test/qjs /tmp/parity-test/node
node dsl/packages/compiler/dist/cli.js build test/fixtures/plans/valid_capabilities.plan.ts --out /tmp/parity-test/qjs
tsx dsl/packages/compiler-node/src/cli.ts test/fixtures/plans/valid_capabilities.plan.ts --out /tmp/parity-test/node
python3 -c "
import json
with open(\"/tmp/parity-test/qjs/valid_capabilities.plan.json\") as f:
    qjs = json.load(f)
with open(\"/tmp/parity-test/node/valid_capabilities.plan.json\") as f:
    node = json.load(f)
del qjs[\"built_by\"]
del node[\"built_by\"]
assert qjs == node, \"Compiler outputs differ\"
"'

run_bg "Test 44: Parity (multiple plans)" bash -c '
mkdir -p /tmp/parity-multi/qjs /tmp/parity-multi/node
for plan in plans/reels_plan_a.plan.ts plans/concat_plan.plan.ts plans/regex_plan.plan.ts; do
    node dsl/packages/compiler/dist/cli.js build "$plan" --out /tmp/parity-multi/qjs
    tsx dsl/packages/compiler-node/src/cli.ts "$plan" --out /tmp/parity-multi/node
done
python3 -c "
import json
for name in [\"reels_plan_a\", \"concat_plan\", \"regex_plan\"]:
    with open(f\"/tmp/parity-multi/qjs/{name}.plan.json\") as f:
        qjs = json.load(f)
    with open(f\"/tmp/parity-multi/node/{name}.plan.json\") as f:
        node = json.load(f)
    del qjs[\"built_by\"]
    del node[\"built_by\"]
    assert qjs == node, f\"{name} outputs differ\"
"'
wait_all

# Batch 10: Engine RFC0001 validation (parallel)
echo "--- Batch 10: Engine RFC0001 validation ---"
run_bg "Test 45: Reject unknown capability" bash -c '
if echo "{}" | engine/bin/rankd --plan artifacts/plans/bad_engine_unknown_cap.plan.json 2>&1 | grep -q "unsupported capability"; then
    exit 0
else
    exit 1
fi
'
run_bg "Test 46: Reject unsorted capabilities" bash -c '
if echo "{}" | engine/bin/rankd --plan artifacts/plans/bad_engine_caps_unsorted.plan.json 2>&1 | grep -q "must be sorted"; then
    exit 0
else
    exit 1
fi
'
run_bg "Test 47: Reject extension not in capabilities" bash -c '
if echo "{}" | engine/bin/rankd --plan artifacts/plans/bad_engine_ext_not_required.plan.json 2>&1 | grep -q "not in capabilities_required"; then
    exit 0
else
    exit 1
fi
'
run_bg "Test 48: Reject node extension without capability" bash -c '
if echo "{}" | engine/bin/rankd --plan artifacts/plans/bad_engine_node_ext_not_declared.plan.json 2>&1 | grep -q "requires plan capability"; then
    exit 0
else
    exit 1
fi
'
run_bg "Test 49: Reject non-empty payload for base capability" bash -c '
if echo "{}" | engine/bin/rankd --plan artifacts/plans/bad_engine_nonempty_payload.plan.json 2>&1 | grep -q "payload must be empty"; then
    exit 0
else
    exit 1
fi
'
wait_all

# Batch 11: capabilities_digest parity tests (parallel)
# Use direct CLI invocation to avoid pnpm lock contention
echo "--- Batch 11: capabilities_digest parity ---"
run_bg "Test 50: Digest parity (with caps)" bash -c '
# Compile plan with dslc (direct invocation)
node dsl/packages/compiler/dist/cli.js build test/fixtures/plans/valid_capabilities.plan.ts --out /tmp/ci-digest-parity >/dev/null 2>&1

# Get C++ digest
CPP_DIGEST=$(engine/bin/rankd --print-plan-info --plan /tmp/ci-digest-parity/valid_capabilities.plan.json | python3 -c "import json,sys; print(json.load(sys.stdin)[\"capabilities_digest\"])")

# Compute TS digest using same algorithm
TS_DIGEST=$(cat /tmp/ci-digest-parity/valid_capabilities.plan.json | python3 -c "
import json, hashlib, sys

def stable_stringify(obj):
    if obj is None:
        return \"null\"
    if isinstance(obj, bool):
        return \"true\" if obj else \"false\"
    if isinstance(obj, str):
        return json.dumps(obj)
    if isinstance(obj, (int, float)):
        return json.dumps(obj)
    if isinstance(obj, list):
        return \"[\" + \",\".join(stable_stringify(x) for x in obj) + \"]\"
    if isinstance(obj, dict):
        keys = sorted(obj.keys())
        pairs = [json.dumps(k) + \":\" + stable_stringify(obj[k]) for k in keys]
        return \"{\" + \",\".join(pairs) + \"}\"
    return json.dumps(obj)

plan = json.load(sys.stdin)
caps = plan.get(\"capabilities_required\", [])
exts = plan.get(\"extensions\", {})
if not caps and not exts:
    print(\"\")
else:
    canonical = {\"capabilities_required\": caps, \"extensions\": exts}
    digest = hashlib.sha256(stable_stringify(canonical).encode()).hexdigest()
    print(f\"sha256:{digest}\")
")

if [ "$CPP_DIGEST" = "$TS_DIGEST" ]; then
    exit 0
else
    echo "Digest mismatch: C++=$CPP_DIGEST TS=$TS_DIGEST"
    exit 1
fi
'

run_bg "Test 51: Digest parity (no caps)" bash -c '
# Get C++ digest for plan without capabilities
CPP_DIGEST=$(engine/bin/rankd --print-plan-info --plan artifacts/plans/reels_plan_a.plan.json | python3 -c "import json,sys; print(json.load(sys.stdin)[\"capabilities_digest\"])")

# Should be empty string
if [ "$CPP_DIGEST" = "" ]; then
    exit 0
else
    echo "Expected empty digest, got: $CPP_DIGEST"
    exit 1
fi
'

run_bg "Test 52: Index has capabilities_digest" bash -c '
# Check that index.json has capabilities_digest field for all plans
python3 << "PYEOF"
import json
with open("artifacts/plans/index.json") as f:
    index = json.load(f)
for plan in index["plans"]:
    if "capabilities_digest" not in plan:
        print("Missing capabilities_digest in plan:", plan["name"])
        exit(1)
print("All plans have capabilities_digest field")
PYEOF
'
wait_all

echo ""
echo "=== All CI tests passed ==="
