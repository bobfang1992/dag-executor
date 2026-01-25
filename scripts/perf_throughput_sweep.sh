#!/usr/bin/env bash
#
# perf_throughput_sweep.sh - Throughput/latency sweep across concurrency levels
#
# Safe for running in resource-capped VMs. Outputs CSV/JSONL for analysis.
#
# Usage:
#   VCPUS=4 DURATION_SEC=10 PLAN_NAME=concat_plan ./scripts/perf_throughput_sweep.sh
#
# Environment variables:
#   VCPUS             - VM vCPU count (default: 4)
#   PLAN_NAME         - Plan to benchmark (default: auto-select offline plan)
#   DURATION_SEC      - Target duration per measurement (default: 10)
#   RUNS              - Repetitions per concurrency level (default: 1)
#   WARMUP_ITERS      - Warmup iterations before measuring (default: 100)
#   CONCURRENCY_LEVELS - Space-separated concurrency values (default: "1 2 4 8 16 32 64 128")
#   ASYNC             - Use async scheduler (default: 1)
#   TIMEOUT_SEC       - Base per-run timeout (default: 30, scales with duration)
#   OUTPUT_DIR        - Output directory (default: ./perf_results)
#   RANKD             - Path to rankd binary (default: engine/bin/rankd)
#   IO_THREADS        - IO thread count for metadata (default: 1)
#
# Output:
#   CSV file with columns: ts,plan,vcpus,cpu_threads,io_threads,async,concurrency,
#                          duration_sec,run_idx,qps,p50_us,p90_us,p99_us,max_us,
#                          ru_maxrss,exit_code,timeout_killed

set -euo pipefail

# ------------------------------------------------------------------
# Configuration (override via environment)
# ------------------------------------------------------------------
VCPUS="${VCPUS:-4}"
PLAN_NAME="${PLAN_NAME:-}"
DURATION_SEC="${DURATION_SEC:-10}"
RUNS="${RUNS:-1}"
WARMUP_ITERS="${WARMUP_ITERS:-100}"
CONCURRENCY_LEVELS="${CONCURRENCY_LEVELS:-1 2 4 8 16 32 64 128}"
ASYNC="${ASYNC:-1}"
TIMEOUT_SEC="${TIMEOUT_SEC:-30}"
OUTPUT_DIR="${OUTPUT_DIR:-./perf_results}"
RANKD="${RANKD:-engine/bin/rankd}"
IO_THREADS="${IO_THREADS:-1}"

# CPU threads should not exceed vCPUs
CPU_THREADS="${CPU_THREADS:-$VCPUS}"
if (( CPU_THREADS > VCPUS )); then
    CPU_THREADS=$VCPUS
fi

# ------------------------------------------------------------------
# Colors and helpers
# ------------------------------------------------------------------
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

log_info()  { echo -e "${BLUE}[INFO]${NC} $*" >&2; }
log_ok()    { echo -e "${GREEN}[OK]${NC} $*" >&2; }
log_warn()  { echo -e "${YELLOW}[WARN]${NC} $*" >&2; }
log_error() { echo -e "${RED}[ERROR]${NC} $*" >&2; }

# Cleanup temp files on exit
TEMP_FILES=()
cleanup() {
    for f in "${TEMP_FILES[@]+"${TEMP_FILES[@]}"}"; do
        [[ -f "$f" ]] && rm -f "$f"
    done
}
trap cleanup EXIT

# Create temp file and track for cleanup
make_temp() {
    local f
    f=$(mktemp)
    TEMP_FILES+=("$f")
    echo "$f"
}

# Platform-agnostic timeout command
# Returns: 0=success, 124=timeout, other=command error
run_with_timeout() {
    local secs=$1
    shift
    if command -v timeout &>/dev/null; then
        timeout --signal=TERM "$secs" "$@"
    elif command -v gtimeout &>/dev/null; then
        gtimeout --signal=TERM "$secs" "$@"
    else
        # Fallback: no timeout
        "$@"
    fi
}

# ------------------------------------------------------------------
# Pre-flight checks
# ------------------------------------------------------------------
if [[ ! -x "$RANKD" ]]; then
    log_error "rankd binary not found at $RANKD"
    log_info "Build with: cmake -S engine -B engine/build -DCMAKE_BUILD_TYPE=Release && cmake --build engine/build --parallel"
    exit 1
fi

# Check for jq
if ! command -v jq &>/dev/null; then
    log_error "jq is required but not found. Install with: brew install jq (macOS) or apt install jq (Linux)"
    exit 1
fi

# List available plans
AVAILABLE_PLANS=$("$RANKD" --list-plans 2>&1 | grep -E '^\s+\S' | awk '{print $1}' || true)
if [[ -z "$AVAILABLE_PLANS" ]]; then
    log_error "No plans found. Ensure artifacts/plans/ contains compiled plans."
    exit 1
fi

# Auto-select offline plan if not specified
OFFLINE_PLANS=("concat_plan" "regex_plan" "parallel_sleep_plan")
if [[ -z "$PLAN_NAME" ]]; then
    for plan in "${OFFLINE_PLANS[@]}"; do
        if echo "$AVAILABLE_PLANS" | grep -qw "$plan"; then
            PLAN_NAME="$plan"
            log_info "Auto-selected offline plan: $PLAN_NAME"
            break
        fi
    done
fi

if [[ -z "$PLAN_NAME" ]]; then
    log_error "No offline-capable plan found and PLAN_NAME not set."
    log_info "Available plans:"
    echo "$AVAILABLE_PLANS" | sed 's/^/  /'
    log_info "Set PLAN_NAME=... to specify a plan"
    exit 1
fi

# Verify plan exists
if ! echo "$AVAILABLE_PLANS" | grep -qw "$PLAN_NAME"; then
    log_error "Plan '$PLAN_NAME' not found."
    log_info "Available plans:"
    echo "$AVAILABLE_PLANS" | sed 's/^/  /'
    exit 1
fi

# ------------------------------------------------------------------
# Setup output
# ------------------------------------------------------------------
mkdir -p "$OUTPUT_DIR"
TIMESTAMP=$(date +%Y%m%d_%H%M%S)
CSV_FILE="$OUTPUT_DIR/sweep_${TIMESTAMP}.csv"
JSONL_FILE="$OUTPUT_DIR/sweep_${TIMESTAMP}.jsonl"
SUMMARY_FILE="$OUTPUT_DIR/sweep_${TIMESTAMP}_summary.txt"

# Write CSV header (stable format for plotting)
echo "ts,plan,vcpus,cpu_threads,io_threads,async,concurrency,duration_sec,run_idx,qps,p50_us,p90_us,p99_us,max_us,ru_maxrss,exit_code,timeout_killed" > "$CSV_FILE"

log_info "Output files:"
log_info "  CSV:     $CSV_FILE"
log_info "  JSONL:   $JSONL_FILE"
log_info "  Summary: $SUMMARY_FILE"

# ------------------------------------------------------------------
# Run benchmark with temp file capture
# ------------------------------------------------------------------
# Runs rankd benchmark, captures stdout to temp file, parses with jq
# Args: iterations concurrency timeout_sec
# Returns via global vars: BENCH_JSON, BENCH_EXIT_CODE, BENCH_TIMEOUT_KILLED
BENCH_JSON=""
BENCH_EXIT_CODE=0
BENCH_TIMEOUT_KILLED=0

run_rankd_bench() {
    local iterations=$1
    local concurrency=$2
    local timeout_secs=$3

    BENCH_JSON=""
    BENCH_EXIT_CODE=0
    BENCH_TIMEOUT_KILLED=0

    local tmp_out tmp_err
    tmp_out=$(make_temp)
    tmp_err=$(make_temp)

    local bench_args=(--bench "$iterations" --bench_concurrency "$concurrency" --cpu_threads "$CPU_THREADS" --plan_name "$PLAN_NAME")
    [[ "$ASYNC" == "1" ]] && bench_args+=(--async_scheduler)

    local rc=0
    set +e
    echo '{"user_id": 1}' | run_with_timeout "$timeout_secs" "$RANKD" "${bench_args[@]}" >"$tmp_out" 2>"$tmp_err"
    rc=$?
    set -e

    BENCH_EXIT_CODE=$rc

    # Check for timeout (exit code 124 from timeout, 137 from SIGKILL)
    if [[ $rc -eq 124 ]] || [[ $rc -eq 137 ]]; then
        log_warn "  Benchmark timed out after ${timeout_secs}s (exit=$rc)"
        BENCH_TIMEOUT_KILLED=1
        return 1
    fi

    # Check for other errors
    if [[ $rc -ne 0 ]]; then
        log_warn "  Benchmark failed with exit code $rc"
        [[ -s "$tmp_err" ]] && log_warn "  stderr: $(head -1 "$tmp_err")"
        return 1
    fi

    # Parse JSON from stdout (rankd prints JSON to stdout, logs to stderr)
    local json
    if ! json=$(jq -c '.' < "$tmp_out" 2>/dev/null); then
        log_warn "  Failed to parse JSON output"
        [[ -n "${DEBUG:-}" ]] && log_info "  Raw output: $(cat "$tmp_out")"
        return 1
    fi

    # Validate we got actual data
    local iters
    iters=$(echo "$json" | jq -r '.iterations // 0')
    if [[ "$iters" == "0" ]] || [[ "$iters" == "null" ]]; then
        log_warn "  Invalid benchmark output (iterations=0)"
        return 1
    fi

    BENCH_JSON="$json"
}

# ------------------------------------------------------------------
# Estimate iterations for target duration
# ------------------------------------------------------------------
estimate_iterations() {
    local concurrency=$1
    local target_sec=$2

    # Quick calibration run (50 iterations, short timeout)
    local calib_iters=50
    local calib_timeout=30

    if ! run_rankd_bench "$calib_iters" "$concurrency" "$calib_timeout"; then
        # Fallback to conservative estimate
        echo "1000"
        return
    fi

    local total_ms
    total_ms=$(echo "$BENCH_JSON" | jq -r '.total_ms // 1000')

    # Calculate iterations for target duration
    # iters = target_ms / (total_ms / calib_iters)
    local ms_per_iter target_iters
    ms_per_iter=$(echo "scale=4; $total_ms / $calib_iters" | bc)
    target_iters=$(echo "scale=0; ($target_sec * 1000) / $ms_per_iter" | bc)

    # Clamp to reasonable range
    # Set MAX_ITERS env var to override (default: 10000).
    local max_iters="${MAX_ITERS:-10000}"
    if (( target_iters < 100 )); then target_iters=100; fi
    if (( target_iters > max_iters )); then target_iters=$max_iters; fi

    echo "$target_iters"
}

# ------------------------------------------------------------------
# Run single benchmark measurement
# ------------------------------------------------------------------
run_bench() {
    local concurrency=$1
    local iterations=$2
    local run_idx=$3

    local ts
    ts=$(date -Iseconds)

    log_info "  Run $run_idx: concurrency=$concurrency, iterations=$iterations"

    # Scale timeout with expected duration: base + 2x expected time + buffer
    local expected_sec=$((DURATION_SEC + 5))
    local run_timeout=$((TIMEOUT_SEC + expected_sec * 2))

    # Run benchmark (sets BENCH_JSON, BENCH_EXIT_CODE, BENCH_TIMEOUT_KILLED)
    if ! run_rankd_bench "$iterations" "$concurrency" "$run_timeout"; then
        # Write CSV row for failed/timeout run
        echo "$ts,$PLAN_NAME,$VCPUS,$CPU_THREADS,$IO_THREADS,$ASYNC,$concurrency,$DURATION_SEC,$run_idx,,,,,,$BENCH_EXIT_CODE,$BENCH_TIMEOUT_KILLED" >> "$CSV_FILE"
        return 1
    fi

    # Extract metrics
    local duration_ms qps avg_us p50_us p90_us p99_us min_us max_us actual_iters
    duration_ms=$(echo "$BENCH_JSON" | jq -r '.total_ms // 0')
    qps=$(echo "$BENCH_JSON" | jq -r '.throughput_rps // 0')
    avg_us=$(echo "$BENCH_JSON" | jq -r '.avg_us // 0')
    p50_us=$(echo "$BENCH_JSON" | jq -r '.p50_us // 0')
    p90_us=$(echo "$BENCH_JSON" | jq -r '.p90_us // ""')  # May not exist
    p99_us=$(echo "$BENCH_JSON" | jq -r '.p99_us // 0')
    min_us=$(echo "$BENCH_JSON" | jq -r '.min_us // 0')
    max_us=$(echo "$BENCH_JSON" | jq -r '.max_us // 0')
    actual_iters=$(echo "$BENCH_JSON" | jq -r '.iterations // 0')

    # p90 may not be in output - estimate from p50/p99 if missing
    if [[ -z "$p90_us" ]] || [[ "$p90_us" == "null" ]]; then
        # Rough estimate: p90 ~= p50 + 0.75*(p99-p50)
        p90_us=$(echo "scale=2; $p50_us + 0.75 * ($p99_us - $p50_us)" | bc)
    fi

    # ru_maxrss not available from rankd output, leave empty
    local ru_maxrss=""

    log_info "    -> ${qps%.*} RPS, p50=${p50_us%.*}us, p99=${p99_us%.*}us"

    # Write CSV row
    echo "$ts,$PLAN_NAME,$VCPUS,$CPU_THREADS,$IO_THREADS,$ASYNC,$concurrency,$DURATION_SEC,$run_idx,$qps,$p50_us,$p90_us,$p99_us,$max_us,$ru_maxrss,$BENCH_EXIT_CODE,$BENCH_TIMEOUT_KILLED" >> "$CSV_FILE"

    # Write JSONL row
    echo "{\"ts\":\"$ts\",\"plan\":\"$PLAN_NAME\",\"vcpus\":$VCPUS,\"cpu_threads\":$CPU_THREADS,\"io_threads\":$IO_THREADS,\"async\":$ASYNC,\"concurrency\":$concurrency,\"duration_sec\":$DURATION_SEC,\"run_idx\":$run_idx,\"qps\":$qps,\"p50_us\":$p50_us,\"p90_us\":$p90_us,\"p99_us\":$p99_us,\"max_us\":$max_us,\"exit_code\":$BENCH_EXIT_CODE,\"timeout_killed\":$BENCH_TIMEOUT_KILLED}" >> "$JSONL_FILE"

    # Return metrics for summary (stdout)
    echo "$concurrency,$qps,$p50_us,$p90_us,$p99_us"
}

# ------------------------------------------------------------------
# Warmup
# ------------------------------------------------------------------
log_info "Configuration:"
log_info "  Plan:        $PLAN_NAME"
log_info "  vCPUs:       $VCPUS"
log_info "  CPU threads: $CPU_THREADS"
log_info "  IO threads:  $IO_THREADS"
log_info "  Duration:    ${DURATION_SEC}s per measurement"
log_info "  Runs:        $RUNS per concurrency level"
log_info "  Async:       $([[ $ASYNC == 1 ]] && echo "yes" || echo "no")"
log_info "  Timeout:     ${TIMEOUT_SEC}s base (scales with duration)"
log_info "  Concurrency: $CONCURRENCY_LEVELS"
echo >&2

log_info "Running warmup ($WARMUP_ITERS iterations)..."
run_rankd_bench "$WARMUP_ITERS" 1 60 || true
warmup_rps=$(echo "$BENCH_JSON" | jq -r '.throughput_rps // 0' 2>/dev/null | xargs printf "%.0f" 2>/dev/null || echo "0")
log_ok "Warmup complete (baseline: ~${warmup_rps} RPS)"
echo >&2

# ------------------------------------------------------------------
# Main sweep
# ------------------------------------------------------------------
declare -a SUMMARY_DATA

log_info "Starting throughput sweep..."
echo >&2

for concurrency in $CONCURRENCY_LEVELS; do
    log_info "Concurrency: $concurrency"

    # Estimate iterations for this concurrency level
    iterations=$(estimate_iterations "$concurrency" "$DURATION_SEC")
    log_info "  Estimated iterations for ${DURATION_SEC}s: $iterations"

    for run_idx in $(seq 1 "$RUNS"); do
        if result=$(run_bench "$concurrency" "$iterations" "$run_idx"); then
            SUMMARY_DATA+=("$result")
        fi
    done
    echo >&2
done

# ------------------------------------------------------------------
# Generate summary
# ------------------------------------------------------------------
log_info "Generating summary..."

{
    echo "========================================"
    echo "Throughput Sweep Summary"
    echo "========================================"
    echo
    echo "Configuration:"
    echo "  Plan:        $PLAN_NAME"
    echo "  vCPUs:       $VCPUS"
    echo "  CPU threads: $CPU_THREADS"
    echo "  IO threads:  $IO_THREADS"
    echo "  Duration:    ${DURATION_SEC}s per measurement"
    echo "  Runs:        $RUNS per concurrency level"
    echo "  Async:       $([[ $ASYNC == 1 ]] && echo "yes" || echo "no")"
    echo "  Timestamp:   $(date)"
    echo
    echo "Results:"
    echo
    printf "%-12s %12s %12s %12s %12s\n" "Concurrency" "QPS" "p50 (us)" "p90 (us)" "p99 (us)"
    printf "%-12s %12s %12s %12s %12s\n" "------------" "------------" "------------" "------------" "------------"

    # Track for knee detection
    prev_qps=0
    prev_p99=0
    knee_concurrency=""
    knee_qps=""

    for row in "${SUMMARY_DATA[@]+"${SUMMARY_DATA[@]}"}"; do
        IFS=',' read -r conc qps p50 p90 p99 <<< "$row"
        qps_int=$(printf "%.0f" "$qps")
        p50_int=$(printf "%.0f" "$p50")
        p90_int=$(printf "%.0f" "$p90")
        p99_int=$(printf "%.0f" "$p99")

        # Knee detection: QPS gain < 5% AND p99 increase > 50%
        if [[ -n "$prev_qps" ]] && (( prev_qps > 0 )) && (( prev_p99 > 0 )); then
            qps_growth=$(echo "scale=4; ($qps_int - $prev_qps) / $prev_qps * 100" | bc)
            p99_growth=$(echo "scale=4; $p99_int / $prev_p99" | bc)
            if (( $(echo "$qps_growth < 5" | bc -l) )) && (( $(echo "$p99_growth > 1.5" | bc -l) )); then
                if [[ -z "$knee_concurrency" ]]; then
                    knee_concurrency="$conc"
                    knee_qps="$qps_int"
                fi
            fi
        fi
        prev_qps=$qps_int
        prev_p99=$p99_int

        printf "%-12s %12s %12s %12s %12s\n" "$conc" "$qps_int" "$p50_int" "$p90_int" "$p99_int"
    done

    echo
    if [[ -n "$knee_concurrency" ]]; then
        echo "Detected saturation knee at concurrency=$knee_concurrency (QPS=$knee_qps)"
        echo "Recommendation: Run at 50-75% of knee for headroom"
    else
        echo "No clear saturation knee detected - may need higher concurrency levels"
    fi
    echo
    echo "Output files:"
    echo "  CSV:   $CSV_FILE"
    echo "  JSONL: $JSONL_FILE"
    echo
    echo "Generate plots with:"
    echo "  python3 scripts/plot_perf_sweep.py --in $CSV_FILE --out $OUTPUT_DIR/plots_${TIMESTAMP}"
    echo
} > "$SUMMARY_FILE"

cat "$SUMMARY_FILE"

log_ok "Sweep complete!"
log_info "CSV output: $CSV_FILE"
