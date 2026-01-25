#!/usr/bin/env bash
#
# Soak test for async timeout race conditions.
# Repeatedly runs must-timeout and mostly-success scenarios to catch
# hangs, leaks, UAF, or race regressions in AsyncWithTimeout/OffloadCpuWithTimeout.
#
# Usage:
#   ./scripts/soak_async_timeout.sh
#   RUNS=50 CONCURRENCY=100 ./scripts/soak_async_timeout.sh
#   PLAN_NAME=my_custom_plan ./scripts/soak_async_timeout.sh
#
# This script is for local stress testing. NOT wired into CI.

set -euo pipefail

# Find timeout command (GNU coreutils)
# On macOS, install via: brew install coreutils
if command -v timeout &>/dev/null; then
    TIMEOUT_CMD="timeout"
elif command -v gtimeout &>/dev/null; then
    TIMEOUT_CMD="gtimeout"
else
    TIMEOUT_CMD=""
    echo "Warning: 'timeout' command not found. Runs will not be protected against hangs."
    echo "On macOS, install via: brew install coreutils"
    echo ""
fi

# Configuration via environment variables
BIN="${BIN:-engine/bin/rankd}"
RUNS="${RUNS:-20}"
CONCURRENCY="${CONCURRENCY:-64}"
DEADLINE_MS_TIMEOUT="${DEADLINE_MS_TIMEOUT:-5}"      # Tiny deadline to force timeouts
DEADLINE_MS_SUCCESS="${DEADLINE_MS_SUCCESS:-5000}"   # Generous deadline for success
NODE_TIMEOUT_MS_TIMEOUT="${NODE_TIMEOUT_MS_TIMEOUT:-3}"   # Tiny node timeout for must-timeout
NODE_TIMEOUT_MS_SUCCESS="${NODE_TIMEOUT_MS_SUCCESS:-0}"   # No node timeout for success (0=disabled)
PLAN_NAME="${PLAN_NAME:-}"
TIMEOUT_PER_RUN="${TIMEOUT_PER_RUN:-30}"  # Seconds before considering a run hung

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

echo "=========================================="
echo "Async Timeout Soak Test"
echo "=========================================="
echo "BIN:                $BIN"
echo "RUNS:               $RUNS"
echo "CONCURRENCY:        $CONCURRENCY"
echo "DEADLINE_MS_TIMEOUT:    $DEADLINE_MS_TIMEOUT"
echo "DEADLINE_MS_SUCCESS:    $DEADLINE_MS_SUCCESS"
echo "NODE_TIMEOUT_MS_TIMEOUT:$NODE_TIMEOUT_MS_TIMEOUT"
echo "NODE_TIMEOUT_MS_SUCCESS:$NODE_TIMEOUT_MS_SUCCESS"
echo "TIMEOUT_PER_RUN:    ${TIMEOUT_PER_RUN}s"
echo "=========================================="

# Verify binary exists
if [[ ! -x "$BIN" ]]; then
    echo -e "${RED}Error: Binary not found or not executable: $BIN${NC}"
    echo "Build the engine first: cmake --build engine/build --parallel"
    exit 1
fi

# Determine plan name
if [[ -z "$PLAN_NAME" ]]; then
    # Check if parallel_sleep_plan exists
    if [[ -f "artifacts/plans/parallel_sleep_plan.plan.json" ]]; then
        PLAN_NAME="parallel_sleep_plan"
    else
        # Fallback: find first plan with "sleep" in name
        PLAN_NAME=$("$BIN" --list-plans 2>/dev/null | grep -i sleep | head -1 | awk '{print $1}' || true)

        if [[ -z "$PLAN_NAME" ]]; then
            echo -e "${RED}Error: No suitable plan found.${NC}"
            echo "Available plans:"
            "$BIN" --list-plans 2>/dev/null || echo "(could not list plans)"
            echo ""
            echo "Set PLAN_NAME env var to specify a plan, or ensure parallel_sleep_plan exists."
            exit 1
        fi
    fi
fi

echo "Using plan: $PLAN_NAME"
echo ""

# Counters
timeout_pass=0
timeout_fail=0
success_pass=0
success_fail=0

# Run a single scenario
# Args: $1=run_index, $2=scenario_name, $3=deadline_ms, $4=node_timeout_ms
run_scenario() {
    local run_idx=$1
    local scenario=$2
    local deadline_ms=$3
    local node_timeout_ms=$4
    local exit_code=0

    # Run with timeout to catch hangs (if timeout command available)
    local cmd_prefix=""
    if [[ -n "$TIMEOUT_CMD" ]]; then
        cmd_prefix="$TIMEOUT_CMD $TIMEOUT_PER_RUN"
    fi

    # Build args (only add node_timeout if non-zero)
    local node_timeout_arg=""
    if [[ "$node_timeout_ms" -gt 0 ]]; then
        node_timeout_arg="--node_timeout_ms $node_timeout_ms"
    fi

    if $cmd_prefix "$BIN" \
        --async_scheduler \
        --plan_name "$PLAN_NAME" \
        --deadline_ms "$deadline_ms" \
        $node_timeout_arg \
        --bench "$CONCURRENCY" \
        >/dev/null 2>&1; then
        exit_code=0
    else
        exit_code=$?
    fi

    # For must-timeout scenario, we expect errors (timeouts), so exit_code != 0 is OK
    # For mostly-success scenario, we expect success, so exit_code == 0 is expected

    if [[ "$scenario" == "must-timeout" ]]; then
        # Any exit (0 or non-0) is fine as long as it doesn't hang
        # Exit code 124 from timeout command means it hung
        if [[ $exit_code -eq 124 ]]; then
            echo -e "  [${run_idx}] ${scenario}: ${RED}HUNG (timeout ${TIMEOUT_PER_RUN}s)${NC}"
            return 1
        else
            echo -e "  [${run_idx}] ${scenario}: ${GREEN}OK${NC} (exit=$exit_code)"
            return 0
        fi
    else
        # mostly-success: expect exit 0
        if [[ $exit_code -eq 0 ]]; then
            echo -e "  [${run_idx}] ${scenario}: ${GREEN}OK${NC}"
            return 0
        elif [[ $exit_code -eq 124 ]]; then
            echo -e "  [${run_idx}] ${scenario}: ${RED}HUNG (timeout ${TIMEOUT_PER_RUN}s)${NC}"
            return 1
        else
            echo -e "  [${run_idx}] ${scenario}: ${YELLOW}FAIL${NC} (exit=$exit_code)"
            return 1
        fi
    fi
}

# Main loop
echo "Running $RUNS iterations..."
echo ""

for ((i=1; i<=RUNS; i++)); do
    echo "Run $i/$RUNS:"

    # Must-timeout scenario (tiny deadline + tiny node timeout forces timeouts + late completions)
    if run_scenario "$i" "must-timeout" "$DEADLINE_MS_TIMEOUT" "$NODE_TIMEOUT_MS_TIMEOUT"; then
        ((timeout_pass++))
    else
        ((timeout_fail++))
    fi

    # Mostly-success scenario (generous deadline, no node timeout)
    if run_scenario "$i" "mostly-success" "$DEADLINE_MS_SUCCESS" "$NODE_TIMEOUT_MS_SUCCESS"; then
        ((success_pass++))
    else
        ((success_fail++))
    fi

    echo ""
done

# Summary
echo "=========================================="
echo "Summary"
echo "=========================================="
echo "must-timeout:   $timeout_pass passed, $timeout_fail failed"
echo "mostly-success: $success_pass passed, $success_fail failed"
echo ""

total_fail=$((timeout_fail + success_fail))
if [[ $total_fail -gt 0 ]]; then
    echo -e "${RED}SOAK FAILED: $total_fail failures${NC}"
    exit 1
else
    echo -e "${GREEN}SOAK PASSED: All $((RUNS * 2)) scenarios completed${NC}"
    exit 0
fi
