#!/usr/bin/env python3
"""Generate SVG positioning chart for async framework comparison."""

import matplotlib
matplotlib.use('Agg')
import matplotlib.pyplot as plt
import numpy as np

# Framework positions (subjective but informed by benchmarks + code size)
# X = Raw Performance (0-10), Y = Simplicity (0-10)
frameworks = {
    'rankd':      (4.5, 9.0),   # v2: ~80% of Folly post, but fastest coro fan-out
    'Boost.Asio': (5.0, 6.5),   # v2: 2.09M post, 1.64M timer, 2.6ms coro
    'Folly':      (6.0, 4.0),   # v2: 3.51M post, 447K timer, 2.0ms coro
    'P2300':      (7.5, 2.5),   # theoretical (io_uring, zero-alloc senders)
    'Seastar':    (9.5, 1.5),   # theoretical (shared-nothing, DPDK)
}

colors = {
    'rankd':      '#2563eb',  # blue - ours
    'Boost.Asio': '#64748b',  # slate
    'Folly':      '#8b5cf6',  # purple
    'P2300':      '#f59e0b',  # amber
    'Seastar':    '#ef4444',  # red
}

fig, ax = plt.subplots(1, 1, figsize=(8, 6))

for name, (x, y) in frameworks.items():
    color = colors[name]
    size = 220 if name == 'rankd' else 140
    zorder = 10 if name == 'rankd' else 5
    ax.scatter(x, y, s=size, c=color, zorder=zorder, edgecolors='white', linewidths=1.5)

    # Offset labels to avoid overlap
    offsets = {
        'rankd':      (-0.15, 0.45),
        'Boost.Asio': (0.3, 0.4),
        'Folly':      (0.3, 0.35),
        'P2300':      (0.3, 0.35),
        'Seastar':    (0.3, 0.35),
    }
    dx, dy = offsets[name]
    weight = 'bold' if name == 'rankd' else 'normal'
    fontsize = 12 if name == 'rankd' else 10.5
    ax.annotate(name, (x, y), xytext=(x + dx, y + dy),
                fontsize=fontsize, fontweight=weight, color=color,
                ha='left' if dx > 0 else 'right')

# Axes
ax.set_xlim(2, 11)
ax.set_ylim(0, 10.5)
ax.set_xlabel('Raw Performance  →', fontsize=12, fontweight='medium')
ax.set_ylabel('Simplicity  →', fontsize=12, fontweight='medium')
ax.set_title('Async Framework Positioning', fontsize=14, fontweight='bold', pad=15)

# Clean up
ax.spines['top'].set_visible(False)
ax.spines['right'].set_visible(False)
ax.set_xticks([])
ax.set_yticks([])

# Add subtle quadrant labels
ax.text(3.0, 9.8, 'Simple & Adequate', fontsize=8, color='#94a3b8', style='italic')
ax.text(8.5, 9.8, 'Simple & Fast', fontsize=8, color='#94a3b8', style='italic')
ax.text(3.0, 0.5, 'Complex & Slow', fontsize=8, color='#94a3b8', style='italic')
ax.text(8.2, 0.5, 'Complex & Fast', fontsize=8, color='#94a3b8', style='italic')

# Light grid
ax.axhline(y=5.25, color='#e2e8f0', linewidth=0.5, linestyle='--', zorder=0)
ax.axvline(x=6.5, color='#e2e8f0', linewidth=0.5, linestyle='--', zorder=0)

plt.tight_layout()

# Save as SVG
out = '/Users/bob/code/dag-executor/docs/assets/async_positioning.svg'
import os
os.makedirs(os.path.dirname(out), exist_ok=True)
fig.savefig(out, format='svg', bbox_inches='tight')
print(f'Saved to {out}')

# Also save PNG for fallback
fig.savefig(out.replace('.svg', '.png'), format='png', dpi=150, bbox_inches='tight')
print(f'Saved PNG too')
