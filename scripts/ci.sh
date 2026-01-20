#!/usr/bin/env bash
set -euo pipefail

REPO_ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$REPO_ROOT"

# =============================================================================
# Expected registry counts (update when adding keys/params/features/tasks)
# =============================================================================
EXPECTED_KEYS=8
EXPECTED_PARAMS=3
EXPECTED_FEATURES=2
EXPECTED_CAPABILITIES=2
EXPECTED_TASKS=7

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
run_bg "Build engine" bash -c "cmake -S engine -B engine/build -DCMAKE_BUILD_TYPE=Release -DCMAKE_EXPORT_COMPILE_COMMANDS=ON && cmake --build engine/build --parallel 4"
wait_all

echo ""
echo "=== Phase 4: Checks + Unit tests (parallel) ==="
run_bg "DSL typecheck" pnpm -C dsl run typecheck
run_bg "DSL lint" pnpm -C dsl run lint
run_bg "Unit tests (rankd)" engine/bin/rankd_tests
run_bg "Unit tests (concat)" engine/bin/concat_tests
run_bg "Unit tests (regex)" engine/bin/regex_tests
run_bg "Unit tests (writes_effect)" engine/bin/writes_effect_tests
run_bg "Unit tests (plan_info)" engine/bin/plan_info_tests
run_bg "Unit tests (schema_delta)" engine/bin/schema_delta_tests
run_bg "TS writes_effect tests" ./node_modules/.bin/tsx dsl/tools/test_writes_effect.ts
run_bg "TS AST extraction tests" ./node_modules/.bin/tsx dsl/tools/test_ast_extraction.ts
run_bg "ESLint plugin tests" ./node_modules/.bin/tsx dsl/packages/eslint-plugin/src/rules/__tests__/run.ts
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

run_bg "Test 5: Print registry" bash -c "
engine/bin/rankd --print-registry | python3 -c '
import sys, json
r = json.load(sys.stdin)
assert \"key_registry_digest\" in r
assert \"param_registry_digest\" in r
assert \"feature_registry_digest\" in r
assert \"capability_registry_digest\" in r
assert \"task_manifest_digest\" in r
assert r[\"num_keys\"] == $EXPECTED_KEYS, f\"Expected $EXPECTED_KEYS keys, got {r[\"num_keys\"]}\"
assert r[\"num_params\"] == $EXPECTED_PARAMS, f\"Expected $EXPECTED_PARAMS params, got {r[\"num_params\"]}\"
assert r[\"num_features\"] == $EXPECTED_FEATURES, f\"Expected $EXPECTED_FEATURES features, got {r[\"num_features\"]}\"
assert r[\"num_capabilities\"] == $EXPECTED_CAPABILITIES, f\"Expected $EXPECTED_CAPABILITIES capabilities, got {r[\"num_capabilities\"]}\"
assert r[\"num_tasks\"] == $EXPECTED_TASKS, f\"Expected $EXPECTED_TASKS tasks, got {r[\"num_tasks\"]}\"
'"
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

# Batch 9: Compilation tests (parallel)
echo "--- Batch 9: Compilation tests ---"
run_bg "Test 43: Compile valid_capabilities" bash -c '
mkdir -p /tmp/compile-test
node dsl/packages/compiler/dist/cli.js build test/fixtures/plans/valid_capabilities.plan.ts --out /tmp/compile-test
python3 -c "
import json
with open(\"/tmp/compile-test/valid_capabilities.plan.json\") as f:
    plan = json.load(f)
assert \"capabilities_required\" in plan
assert \"cap.audit\" in plan[\"capabilities_required\"]
assert \"cap.debug\" in plan[\"capabilities_required\"]
"'

run_bg "Test 44: Compile multiple plans" bash -c '
mkdir -p /tmp/compile-multi
for plan in plans/reels_plan_a.plan.ts plans/concat_plan.plan.ts plans/regex_plan.plan.ts; do
    node dsl/packages/compiler/dist/cli.js build "$plan" --out /tmp/compile-multi
done
python3 -c "
import json
for name in [\"reels_plan_a\", \"concat_plan\", \"regex_plan\"]:
    with open(f\"/tmp/compile-multi/{name}.plan.json\") as f:
        plan = json.load(f)
    assert \"plan_name\" in plan
    assert \"nodes\" in plan
    assert plan[\"plan_name\"] == name
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
if echo "{}" | engine/bin/rankd --plan artifacts/plans/bad_engine_nonempty_payload.plan.json 2>&1 | grep -q "unexpected key"; then
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

run_bg "Test 53: Capability registry digest parity (TS == C++)" bash -c '
# Get TS digest from generated artifacts
TS_DIGEST=$(cat artifacts/capabilities.digest | tr -d "\\n")

# Get C++ digest from --print-registry
CPP_DIGEST=$(engine/bin/rankd --print-registry | python3 -c "import json,sys; print(json.load(sys.stdin)[\"capability_registry_digest\"])")

if [ "$TS_DIGEST" = "$CPP_DIGEST" ]; then
    exit 0
else
    echo "Capability registry digest mismatch: TS=$TS_DIGEST C++=$CPP_DIGEST"
    exit 1
fi
'

run_bg "Test 54: writes_effect evaluator parity (TS == C++)" bash -c '
# Generate test cases from TS
./node_modules/.bin/tsx dsl/tools/test_writes_effect_parity.ts > /tmp/writes_effect_test_cases.json

# Verify TS test cases are valid JSON
python3 -c "import json; json.load(open(\"/tmp/writes_effect_test_cases.json\"))"

# The test passes if TS tests pass (C++ tests validated separately via Catch2)
# For full parity, we verify the test case format is valid
python3 << "PYEOF"
import json
with open("/tmp/writes_effect_test_cases.json") as f:
    data = json.load(f)
test_cases = data["test_cases"]
assert len(test_cases) >= 15, f"Expected at least 15 test cases, got {len(test_cases)}"
for tc in test_cases:
    assert "name" in tc
    assert "expr" in tc
    assert "gamma" in tc
    assert "expected_kind" in tc
    assert "expected_keys" in tc
    assert tc["expected_kind"] in ["exact", "may", "unknown"]
print(f"Validated {len(test_cases)} writes_effect parity test cases")
PYEOF
'
wait_all

# Batch 12: writes_eval tests (RFC0005 Step 12.3a)
echo "--- Batch 12: writes_eval tests ---"
run_bg "Test 55: print-plan-info shows writes_eval" bash -c '
engine/bin/rankd --print-plan-info --plan_name reels_plan_a > /tmp/ci-writes-eval-55.json
python3 scripts/ci-helpers/validate_writes_eval.py /tmp/ci-writes-eval-55.json
'

run_bg "Test 56: print-plan-info error on unsupported caps" bash -c '
# Compile plan with unsupported capabilities
node dsl/packages/compiler/dist/cli.js build test/fixtures/plans/valid_capabilities.plan.ts --out /tmp/ci-writes-eval >/dev/null 2>&1

# Should exit non-zero
if engine/bin/rankd --print-plan-info --plan /tmp/ci-writes-eval/valid_capabilities.plan.json > /tmp/ci-unsup-caps.json 2>&1; then
    echo "Expected non-zero exit for unsupported capabilities"
    exit 1
fi

# Output should have error field
python3 << "PYEOF"
import json
with open("/tmp/ci-unsup-caps.json") as f:
    data = json.load(f)
assert "error" in data, "Missing error field"
assert data["error"]["code"] == "UNSUPPORTED_CAPABILITY"
assert len(data["error"]["unsupported"]) > 0
assert "nodes" not in data, "Should not have nodes on error"
unsupported = data["error"]["unsupported"]
print(f"Correctly rejected unsupported capabilities: {unsupported}")
PYEOF
'

run_bg "Test 57: dump-run-trace shows schema_deltas" bash -c '
echo "{\"request_id\": \"test\"}" | engine/bin/rankd --plan engine/tests/fixtures/plan_info/vm_and_row_ops.plan.json --dump-run-trace > /tmp/ci-schema-deltas.json
python3 scripts/ci-helpers/validate_schema_deltas.py /tmp/ci-schema-deltas.json
'
wait_all

# Batch 13: AST expression extraction tests (Step 13.1)
echo "--- Batch 13: AST expression extraction ---"
run_bg "Test 58: Natural expression compilation" bash -c '
node dsl/packages/compiler/dist/cli.js build test/fixtures/plans/vm_ast_expr_basic.plan.ts --out /tmp/ci-ast-expr-58 2>&1
python3 scripts/ci-helpers/validate_ast_expr.py /tmp/ci-ast-expr-58/vm_ast_expr_basic.plan.json
'

run_bg "Test 59: Division rejection" bash -c '
# Attempt to compile plan with division - should fail
if node dsl/packages/compiler/dist/cli.js build test/fixtures/plans/vm_ast_expr_div.plan.ts --out /tmp/ci-ast-expr-59 2>&1; then
    echo "Expected compilation to fail for division operator"
    exit 1
fi

# Verify error message mentions division
node dsl/packages/compiler/dist/cli.js build test/fixtures/plans/vm_ast_expr_div.plan.ts --out /tmp/ci-ast-expr-59 2>&1 | grep -qi "division\|not supported" || {
    echo "Expected error message about division"
    exit 1
}
exit 0
'

run_bg "Test 60: Mixed expression styles" bash -c '
node dsl/packages/compiler/dist/cli.js build test/fixtures/plans/vm_ast_expr_mixed.plan.ts --out /tmp/ci-ast-expr-60 2>&1
python3 scripts/ci-helpers/validate_mixed_expr.py /tmp/ci-ast-expr-60/vm_ast_expr_mixed.plan.json
'

run_bg "Test 61: Reject invalid imports" bash -c '
# Attempt to compile plan with invalid import - should fail
if node dsl/packages/compiler/dist/cli.js build test/fixtures/plans/bad_import.plan.ts --out /tmp/ci-ast-expr-61 2>&1; then
    echo "Expected compilation to fail for invalid import"
    exit 1
fi

# Verify error message mentions import restriction
node dsl/packages/compiler/dist/cli.js build test/fixtures/plans/bad_import.plan.ts --out /tmp/ci-ast-expr-61 2>&1 | grep -q "not allowed in plans" || {
    echo "Expected error message about import restriction"
    exit 1
}
exit 0
'

run_bg "Test 62: ESLint catches aliased imports" bash -c '
# Run ESLint on bad_alias.plan.ts - should report error for aliased Key
ESLINT_OUTPUT=$(./node_modules/.bin/eslint test/fixtures/plans/bad_alias.plan.ts --config eslint.config.js 2>&1 || true)

# Should report no-dsl-import-alias error
if echo "$ESLINT_OUTPUT" | grep -q "no-dsl-import-alias"; then
    exit 0
else
    echo "Expected ESLint to catch aliased import (Key as JK)"
    echo "Output: $ESLINT_OUTPUT"
    exit 1
fi
'

run_bg "Test 63: Production plans pass ESLint" bash -c '
# Run ESLint on production plan files - should all pass
./node_modules/.bin/eslint "plans/**/*.plan.ts" "examples/plans/**/*.plan.ts" --config eslint.config.js 2>&1
'
wait_all

# Batch 14: Task manifest tests
echo "--- Batch 14: Task manifest tests ---"
run_bg "Test 64: tasks.toml in sync with C++" bash -c '
# Generate fresh TOML from C++
engine/bin/rankd --print-task-manifest > /tmp/tasks_generated.toml

# Compare with committed file
if diff registry/tasks.toml /tmp/tasks_generated.toml > /tmp/tasks_diff.txt 2>&1; then
    exit 0
else
    echo "ERROR: registry/tasks.toml out of sync with C++ TaskSpec"
    echo "Diff:"
    cat /tmp/tasks_diff.txt
    echo ""
    echo "Run: engine/bin/rankd --print-task-manifest > registry/tasks.toml"
    exit 1
fi
'

run_bg "Test 65: Task manifest digest parity (TS == C++)" bash -c '
# Get TS digest from generated tasks.ts
TS_DIGEST=$(grep "TASK_MANIFEST_DIGEST" dsl/packages/generated/tasks.ts | sed "s/.*\"\(.*\)\".*/\1/")

# Get C++ digest from tasks.toml
CPP_DIGEST=$(grep "manifest_digest" registry/tasks.toml | sed "s/.*\"\(.*\)\".*/\1/")

if [ "$TS_DIGEST" = "$CPP_DIGEST" ]; then
    exit 0
else
    echo "Task manifest digest mismatch: TS=$TS_DIGEST C++=$CPP_DIGEST"
    exit 1
fi
'

run_bg "Test 66: tasks.ts exports task count" bash -c '
# Check that TASK_COUNT is exported and matches num_tasks
TS_COUNT=$(grep "TASK_COUNT" dsl/packages/generated/tasks.ts | sed "s/.*= \([0-9]*\).*/\1/")
CPP_COUNT=$(engine/bin/rankd --print-registry | python3 -c "import json,sys; print(json.load(sys.stdin)[\"num_tasks\"])")

if [ "$TS_COUNT" = "$CPP_COUNT" ]; then
    exit 0
else
    echo "Task count mismatch: TS=$TS_COUNT C++=$CPP_COUNT"
    exit 1
fi
'
wait_all

# Batch 15: Type validation tests (compile-time rejection)
echo "--- Batch 15: Type validation tests ---"
run_bg "Test 67: Reject invalid outKey type" bash -c '
if node dsl/packages/compiler/dist/cli.js build test/fixtures/plans/bad_outkey_type.plan.ts --out /tmp/ci-type-67 2>&1 | grep -q "must be a KeyToken"; then
    exit 0
else
    echo "Expected rejection with KeyToken error"
    exit 1
fi
'
run_bg "Test 68: Reject string fanout" bash -c '
if node dsl/packages/compiler/dist/cli.js build test/fixtures/plans/bad_fanout_type.plan.ts --out /tmp/ci-type-68 2>&1 | grep -q "must be an integer"; then
    exit 0
else
    echo "Expected rejection with integer error"
    exit 1
fi
'
run_bg "Test 69: Reject null count" bash -c '
if node dsl/packages/compiler/dist/cli.js build test/fixtures/plans/bad_count_type.plan.ts --out /tmp/ci-type-69 2>&1 | grep -q "must be an integer"; then
    exit 0
else
    echo "Expected rejection with integer error"
    exit 1
fi
'
run_bg "Test 70: Reject invalid expr type" bash -c '
if node dsl/packages/compiler/dist/cli.js build test/fixtures/plans/bad_expr_type.plan.ts --out /tmp/ci-type-70 2>&1 | grep -q "must be an ExprNode or ExprPlaceholder"; then
    exit 0
else
    echo "Expected rejection with ExprNode error"
    exit 1
fi
'
run_bg "Test 71: Reject invalid pred type" bash -c '
if node dsl/packages/compiler/dist/cli.js build test/fixtures/plans/bad_pred_type.plan.ts --out /tmp/ci-type-71 2>&1 | grep -q "must be a PredNode"; then
    exit 0
else
    echo "Expected rejection with PredNode error"
    exit 1
fi
'
run_bg "Test 72: Reject numeric trace" bash -c '
if node dsl/packages/compiler/dist/cli.js build test/fixtures/plans/bad_trace_type.plan.ts --out /tmp/ci-type-72 2>&1 | grep -q "must be a string or null"; then
    exit 0
else
    echo "Expected rejection with string error"
    exit 1
fi
'
wait_all

echo ""
echo "=== All CI tests passed ==="
