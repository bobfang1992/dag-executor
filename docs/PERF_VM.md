# Performance Testing in a Resource-Capped VM

This guide explains how to run performance stress tests safely inside a QEMU/UTM virtual machine without overwhelming the host.

## Why Test in a VM?

- **Isolation**: Prevents runaway benchmarks from affecting host system
- **Reproducibility**: Fixed vCPU/RAM makes results comparable across runs
- **Safety**: Resource caps prevent thermal throttling and fan noise
- **Profiling**: Easier to correlate metrics with known resource limits

## VM Setup (UTM on Apple Silicon)

### Recommended Settings

| Setting | Default | Range | Notes |
|---------|---------|-------|-------|
| vCPUs | 4 | 2–6 | Match `--cpu_threads` to this |
| RAM | 8 GB | 4–16 GB | More helps with large plans |
| Virtualization | HVF | — | Enable Apple Hypervisor Framework |
| Disk | 20 GB | 10–40 GB | Enough for Linux + build tools |

### UTM Configuration Steps

1. **Create new VM**:
   - Type: Linux
   - Architecture: ARM64 (aarch64)
   - Enable "Use Apple Virtualization"

2. **System settings**:
   - CPU: 4 cores (adjustable)
   - Memory: 8192 MB
   - Enable "Force Multicore" if available

3. **Shared folder** (for repo access):
   - Add a VirtIO shared directory
   - Source: Your `dag-executor` repo path
   - Tag: `dag-executor`
   - Mount in guest: `sudo mount -t virtiofs dag-executor /mnt/dag-executor`

4. **Guest OS**: Ubuntu 24.04 ARM64 or similar

### QEMU Command Line (Alternative)

```bash
qemu-system-aarch64 \
  -machine virt,accel=hvf \
  -cpu host \
  -smp cpus=4 \
  -m 8G \
  -drive file=ubuntu-arm64.qcow2,if=virtio \
  -virtfs local,path=/path/to/dag-executor,mount_tag=repo,security_model=mapped \
  -nographic
```

Mount in guest:
```bash
sudo mount -t 9p -o trans=virtio repo /mnt/dag-executor
```

## Safe Benchmark Defaults

The sweep script enforces these limits to prevent runaway tests:

| Limit | Default | Purpose |
|-------|---------|---------|
| Per-run timeout | 60s | Kill hung benchmarks |
| Max duration | 30s | Keep individual runs short |
| Warmup | 100 iterations | Stabilize before measuring |
| Concurrency cap | 128 | Prevent thread explosion |
| CPU threads | ≤ vCPUs | Don't oversubscribe |

## Running the Throughput Sweep

### Quick Start

```bash
# Inside the VM, from repo root
cd /mnt/dag-executor

# Build Release if needed
cmake -S engine -B engine/build -DCMAKE_BUILD_TYPE=Release
cmake --build engine/build --parallel

# Run sweep with defaults
./scripts/perf_throughput_sweep.sh

# Or with custom settings
VCPUS=4 DURATION_SEC=10 RUNS=3 PLAN_NAME=concat_plan ./scripts/perf_throughput_sweep.sh
```

### Environment Variables

| Variable | Default | Description |
|----------|---------|-------------|
| `VCPUS` | 4 | Match to VM vCPU count |
| `PLAN_NAME` | concat_plan | Plan to benchmark (must be offline-capable) |
| `DURATION_SEC` | 10 | Seconds per measurement (iterations auto-calculated) |
| `RUNS` | 1 | Repetitions per concurrency level |
| `WARMUP_ITERS` | 100 | Warmup iterations before measuring |
| `CONCURRENCY_LEVELS` | "1 2 4 8 16 32 64 128" | Space-separated concurrency values |
| `ASYNC` | 1 | Use async scheduler (1) or sync (0) |
| `TIMEOUT_SEC` | 60 | Per-run timeout to kill hung benchmarks |
| `OUTPUT_DIR` | `./perf_results` | Where to write CSV/JSON output |

### Output Files

The script generates:
- `perf_results/sweep_<timestamp>.csv` - Main results (one row per run)
- `perf_results/sweep_<timestamp>.jsonl` - Same data as JSON Lines
- `perf_results/sweep_<timestamp>_summary.txt` - Human-readable summary

### CSV Columns

```
ts,plan,vcpus,cpu_threads,io_threads,async,concurrency,duration_sec,run_idx,qps,p50_us,p90_us,p99_us,max_us,ru_maxrss,exit_code,timeout_killed
```

| Column | Description |
|--------|-------------|
| `ts` | ISO timestamp |
| `plan` | Plan name |
| `vcpus` | VM vCPU count |
| `cpu_threads` | CPU thread pool size |
| `io_threads` | IO thread count (metadata) |
| `async` | 1=async scheduler, 0=sync |
| `concurrency` | Concurrent requests |
| `duration_sec` | Target duration setting |
| `run_idx` | Run index (1, 2, ... RUNS) |
| `qps` | Throughput (requests/sec) |
| `p50_us` | Median latency (microseconds) |
| `p90_us` | 90th percentile latency |
| `p99_us` | 99th percentile latency |
| `max_us` | Maximum latency |
| `ru_maxrss` | Peak RSS (if available) |
| `exit_code` | Process exit code |
| `timeout_killed` | 1 if killed by timeout |

## Generating Plots

After running the sweep, generate visualization plots:

```bash
# Install matplotlib if needed
pip install matplotlib

# Generate plots from CSV
python3 scripts/plot_perf_sweep.py \
  --in perf_results/sweep_<timestamp>.csv \
  --out perf_results/plots_<timestamp>/
```

This produces:

| File | Description |
|------|-------------|
| `qps_vs_concurrency.png` | QPS scaling curve with knee marker |
| `latency_vs_concurrency.png` | p50/p90/p99 latency curves |
| `qps_vs_p99.png` | Pareto frontier (QPS vs tail latency) |
| `knee_summary.txt` | Detected knee point and recommendation |

### Example Workflow

```bash
# Run sweep in VM
RUNS=3 DURATION_SEC=10 VCPUS=4 ./scripts/perf_throughput_sweep.sh

# Plot on host (or in VM if matplotlib installed)
python3 scripts/plot_perf_sweep.py \
  --in perf_results/sweep_20260125_120000.csv \
  --out perf_results/plots_20260125_120000/

# View results
open perf_results/plots_20260125_120000/*.png  # macOS
xdg-open perf_results/plots_20260125_120000/*.png  # Linux
```

## Interpreting Results

### Finding the "Knee"

The sweep helps identify the **saturation point** where:
1. **QPS plateaus** - Adding more concurrency doesn't increase throughput
2. **p99 latency spikes** - Tail latency grows sharply

Example output interpretation:

```
Concurrency |    QPS    |  p50 (us) |  p99 (us) | Assessment
------------|-----------|-----------|-----------|------------
     1      |    1,200  |     800   |    1,500  | Underutilized
     4      |    4,500  |     850   |    1,800  | Near-linear scaling
     8      |    8,200  |     950   |    2,500  | Good scaling
    16      |   12,500  |   1,200   |    5,000  | Starting to saturate
    32      |   14,000  |   2,100   |   15,000  | Saturated (knee) ←
    64      |   14,200  |   4,200   |   35,000  | Over-saturated
   128      |   14,100  |   8,500   |   80,000  | Severely over-saturated
```

The **knee** at concurrency=32 shows:
- QPS barely increases (14,000 → 14,200)
- p99 jumps 3x (15ms → 35ms)

**Recommendation**: Run at 50–75% of the knee point for headroom.

### Scaling vs vCPUs

Run sweeps with different `VCPUS` values to see scaling:

```bash
for v in 2 4 6; do
  VCPUS=$v ./scripts/perf_throughput_sweep.sh
done
```

Ideal scaling: 2x vCPUs ≈ 2x throughput (until memory/IO bottleneck).

### Memory Usage

Watch `ru_maxrss_kb` to ensure you're not swapping:
- If RSS approaches VM RAM limit, reduce concurrency or increase VM RAM
- Sudden throughput drops often indicate swap thrashing

## Offline-Capable Plans

These plans work without Redis:

| Plan | Description | Offline |
|------|-------------|---------|
| `concat_plan` | Basic concat operations | ✅ |
| `regex_plan` | Regex filtering | ✅ |
| `reels_plan_a` | Full pipeline with Redis | ❌ |
| `parallel_sleep_plan` | Sleep-based latency test | ✅ |

The script auto-selects `concat_plan` if available, or prompts you to specify one.

## Troubleshooting

### "Benchmark timed out"
- Reduce `DURATION_SEC` or `--bench` iteration count
- Check if plan requires Redis (switch to offline plan)

### Low throughput inside VM
- Verify HVF acceleration is enabled (`sysctl kern.hv_support`)
- Ensure `--cpu_threads` matches VM vCPUs
- Check guest isn't CPU-throttled (run `top` during benchmark)

### Results vary wildly
- Increase `WARMUP_ITERS` to 500+
- Run `RUNS=5` and look at median
- Disable host background tasks (Spotlight, Time Machine)

### VM is slow to start
- Use virtio disk instead of USB emulation
- Pre-build the engine on host, share via virtiofs

## Example Session

```bash
# Inside VM
cd /mnt/dag-executor

# Quick sanity check (10 iterations, should complete instantly)
echo '{"user_id": 1}' | engine/bin/rankd --bench 10 --plan_name concat_plan

# Full sweep (takes ~5 minutes with defaults)
VCPUS=4 DURATION_SEC=10 ./scripts/perf_throughput_sweep.sh

# View results
cat perf_results/sweep_*_summary.txt

# Generate plots
pip install matplotlib  # if not installed
python3 scripts/plot_perf_sweep.py \
  --in perf_results/sweep_*.csv \
  --out perf_results/plots/

# Open plots
ls perf_results/plots/
```

## See Also

- [docs/EVENT_LOOP_BENCH.md](EVENT_LOOP_BENCH.md) - EventLoop micro-benchmarks
- [docs/THREADING_MODEL.md](THREADING_MODEL.md) - Thread pool architecture
- [docs/async_dag_scheduler_architecture.md](async_dag_scheduler_architecture.md) - Async scheduler design
