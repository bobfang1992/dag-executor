#!/usr/bin/env python3
"""
plot_perf_sweep.py - Generate plots from throughput sweep CSV

Reads CSV from perf_throughput_sweep.sh and produces:
  - qps_vs_concurrency.png     : QPS scaling with concurrency
  - latency_vs_concurrency.png : p50/p90/p99 latency curves
  - qps_vs_p99.png             : Pareto frontier (QPS vs tail latency)
  - knee_summary.txt           : Detected knee point (if any)

Usage:
  python3 scripts/plot_perf_sweep.py --in perf_results/sweep_*.csv --out perf_results/plots_*/
"""

import argparse
import csv
import os
import statistics
from collections import defaultdict
from typing import Optional


def median(xs: list) -> Optional[float]:
    """Compute median, ignoring None values."""
    xs = [x for x in xs if x is not None]
    return statistics.median(xs) if xs else None


def parse_float(s: str) -> Optional[float]:
    """Parse float, return None on failure."""
    if not s or s == "null":
        return None
    try:
        return float(s)
    except (ValueError, TypeError):
        return None


def parse_int(s: str) -> Optional[int]:
    """Parse int, return None on failure."""
    if not s or s == "null":
        return None
    try:
        return int(float(s))
    except (ValueError, TypeError):
        return None


def detect_knee(conc: list, qps: list, p99: list) -> Optional[int]:
    """
    Detect saturation knee point.

    Heuristic: Find smallest concurrency c such that:
      - qps_gain(c -> next) < 5%
      - p99(next) / p99(c) > 1.5x
    """
    for i in range(len(conc) - 1):
        if qps[i] is None or qps[i + 1] is None:
            continue
        if p99[i] is None or p99[i + 1] is None or p99[i] == 0:
            continue

        qps_gain = (qps[i + 1] - qps[i]) / qps[i] * 100
        p99_ratio = p99[i + 1] / p99[i]

        if qps_gain < 5 and p99_ratio > 1.5:
            return conc[i]

    return None


def main():
    parser = argparse.ArgumentParser(description="Generate plots from sweep CSV")
    parser.add_argument("--in", dest="inp", required=True, help="Input CSV file")
    parser.add_argument("--out", dest="out", required=True, help="Output directory")
    args = parser.parse_args()

    # Import matplotlib (may not be available)
    try:
        import matplotlib
        matplotlib.use('Agg')  # Non-interactive backend
        import matplotlib.pyplot as plt
    except ImportError:
        print("ERROR: matplotlib not installed. Install with: pip install matplotlib")
        return 1

    os.makedirs(args.out, exist_ok=True)

    # Read CSV
    rows = []
    with open(args.inp, newline="") as f:
        reader = csv.DictReader(f)
        for row in reader:
            # Skip timeout/failed runs
            if row.get("timeout_killed") == "1":
                continue
            if not row.get("qps"):
                continue
            rows.append(row)

    if not rows:
        print("ERROR: No valid data rows found in CSV")
        return 1

    # Aggregate by concurrency (median across runs)
    by_conc = defaultdict(list)
    meta = {}

    for row in rows:
        c = parse_int(row.get("concurrency", "0"))
        if c is None or c == 0:
            continue
        by_conc[c].append(row)

        # Capture metadata from first valid row
        for k in ["plan", "vcpus", "cpu_threads", "io_threads", "async"]:
            if k in row and row[k] and k not in meta:
                meta[k] = row[k]

    # Sort concurrency levels
    conc = sorted(by_conc.keys())

    # Compute medians
    qps = []
    p50 = []
    p90 = []
    p99 = []
    max_lat = []

    for c in conc:
        qps.append(median([parse_float(x.get("qps", "")) for x in by_conc[c]]))
        p50.append(median([parse_float(x.get("p50_us", "")) for x in by_conc[c]]))
        p90.append(median([parse_float(x.get("p90_us", "")) for x in by_conc[c]]))
        p99.append(median([parse_float(x.get("p99_us", "")) for x in by_conc[c]]))
        max_lat.append(median([parse_float(x.get("max_us", "")) for x in by_conc[c]]))

    # Build title suffix
    title_parts = []
    if meta.get("plan"):
        title_parts.append(f'plan={meta["plan"]}')
    if meta.get("vcpus"):
        title_parts.append(f'vcpus={meta["vcpus"]}')
    if meta.get("cpu_threads"):
        title_parts.append(f'cpu={meta["cpu_threads"]}')
    if meta.get("async"):
        title_parts.append(f'async={meta["async"]}')
    title_suffix = " ".join(title_parts)

    # Detect knee
    knee = detect_knee(conc, qps, p99)
    knee_idx = conc.index(knee) if knee else None

    # =========================================================================
    # Plot A: QPS vs Concurrency
    # =========================================================================
    plt.figure(figsize=(10, 6))
    plt.plot(conc, qps, marker="o", linewidth=2, markersize=8, color="#2196F3")

    # Mark knee point
    if knee_idx is not None:
        plt.axvline(x=knee, color="red", linestyle="--", linewidth=1.5,
                   label=f"Knee @ c={knee}")
        plt.legend()

    plt.xlabel("Concurrency", fontsize=12)
    plt.ylabel("QPS (req/s)", fontsize=12)
    plt.title(f"QPS vs Concurrency\n{title_suffix}", fontsize=14)
    plt.grid(True, alpha=0.3)
    plt.tight_layout()
    plt.savefig(os.path.join(args.out, "qps_vs_concurrency.png"), dpi=150)
    plt.close()
    print(f"  Created: qps_vs_concurrency.png")

    # =========================================================================
    # Plot B: Latency vs Concurrency
    # =========================================================================
    plt.figure(figsize=(10, 6))

    if any(p50):
        plt.plot(conc, p50, marker="o", linewidth=2, markersize=6,
                label="p50", color="#4CAF50")
    if any(p90):
        plt.plot(conc, p90, marker="s", linewidth=2, markersize=6,
                label="p90", color="#FF9800")
    if any(p99):
        plt.plot(conc, p99, marker="^", linewidth=2, markersize=6,
                label="p99", color="#F44336")

    # Mark knee point
    if knee_idx is not None:
        plt.axvline(x=knee, color="red", linestyle="--", linewidth=1.5, alpha=0.7)

    plt.xlabel("Concurrency", fontsize=12)
    plt.ylabel("Latency (us)", fontsize=12)
    plt.title(f"Latency vs Concurrency\n{title_suffix}", fontsize=14)
    plt.legend(loc="upper left")
    plt.grid(True, alpha=0.3)

    # Use log scale if range is large
    if p99:
        valid_p99 = [x for x in p99 if x]
        if valid_p99 and max(valid_p99) / min(valid_p99) > 10:
            plt.yscale("log")

    plt.tight_layout()
    plt.savefig(os.path.join(args.out, "latency_vs_concurrency.png"), dpi=150)
    plt.close()
    print(f"  Created: latency_vs_concurrency.png")

    # =========================================================================
    # Plot C: QPS vs p99 (Pareto frontier)
    # =========================================================================
    plt.figure(figsize=(10, 6))

    # Build (p99, qps, concurrency) tuples
    pairs = [(p99[i], qps[i], conc[i]) for i in range(len(conc))
             if p99[i] and qps[i]]
    pairs.sort()  # Sort by p99 ascending

    xs = [p[0] for p in pairs]
    ys = [p[1] for p in pairs]
    labels = [p[2] for p in pairs]

    # Color by concurrency
    colors = plt.cm.viridis([i / len(pairs) for i in range(len(pairs))])

    plt.scatter(xs, ys, c=colors, s=100, edgecolors='black', linewidth=0.5)
    plt.plot(xs, ys, linestyle="--", alpha=0.5, color="gray")

    # Annotate points with concurrency
    for x, y, label in pairs:
        plt.annotate(f"c={label}", (x, y), textcoords="offset points",
                    xytext=(5, 5), fontsize=9)

    # Mark knee point
    if knee:
        knee_p99 = p99[knee_idx]
        knee_qps = qps[knee_idx]
        if knee_p99 and knee_qps:
            plt.scatter([knee_p99], [knee_qps], color="red", s=200,
                       marker="*", zorder=5, label=f"Knee @ c={knee}")
            plt.legend()

    plt.xlabel("p99 Latency (us)", fontsize=12)
    plt.ylabel("QPS (req/s)", fontsize=12)
    plt.title(f"QPS vs p99 (Pareto Frontier)\n{title_suffix}", fontsize=14)
    plt.grid(True, alpha=0.3)
    plt.tight_layout()
    plt.savefig(os.path.join(args.out, "qps_vs_p99.png"), dpi=150)
    plt.close()
    print(f"  Created: qps_vs_p99.png")

    # =========================================================================
    # Write knee summary
    # =========================================================================
    summary_path = os.path.join(args.out, "knee_summary.txt")
    with open(summary_path, "w") as f:
        f.write("Knee Detection Summary\n")
        f.write("=" * 50 + "\n\n")
        f.write(f"Input: {args.inp}\n")
        f.write(f"Configuration: {title_suffix}\n\n")

        if knee:
            f.write(f"Detected knee at concurrency = {knee}\n")
            f.write(f"  QPS at knee: {qps[knee_idx]:.0f} req/s\n")
            f.write(f"  p99 at knee: {p99[knee_idx]:.0f} us\n\n")
            f.write(f"Recommendation: Run at 50-75% of knee concurrency\n")
            f.write(f"  Suggested range: {int(knee * 0.5)} - {int(knee * 0.75)}\n")
        else:
            f.write("No clear knee detected.\n")
            f.write("Consider running with higher concurrency levels.\n")

        f.write("\n")
        f.write("Data points:\n")
        f.write(f"{'Concurrency':>12} {'QPS':>12} {'p50 (us)':>12} {'p90 (us)':>12} {'p99 (us)':>12}\n")
        f.write("-" * 64 + "\n")
        for i, c in enumerate(conc):
            f.write(f"{c:>12} {qps[i] or 0:>12.0f} {p50[i] or 0:>12.0f} {p90[i] or 0:>12.0f} {p99[i] or 0:>12.0f}\n")

    print(f"  Created: knee_summary.txt")

    print(f"\nPlots written to: {args.out}/")
    if knee:
        print(f"Detected knee at concurrency = {knee}")

    return 0


if __name__ == "__main__":
    exit(main())
