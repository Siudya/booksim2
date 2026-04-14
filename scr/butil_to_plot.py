#!/usr/bin/env python3
"""Convert butil CSV files into a grouped bar chart (PDF or SVG)."""
from __future__ import annotations

import argparse
import csv
import sys
from pathlib import Path

import matplotlib.pyplot as plt
import numpy as np

TOPOLOGY_ORDER = ["Mesh", "P2P", "Twin"]
SERIES_NAMES = ["ReD", "VDA", "VNS", "MinVNS"]
SERIES_COLORS = {
    "ReD":    "#1f77b4",
    "VDA":    "#ff7f0e",
    "VNS":    "#2ca02c",
    "MinVNS": "#d62728",
}


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Convert butil CSV files into a grouped bar chart."
    )
    parser.add_argument(
        "--csv-dir", "-c", default="butil", type=Path,
        help="Directory containing <traffic>.csv files (default: butil).",
    )
    parser.add_argument(
        "--output", "-o", default="butil.svg", type=Path,
        help="Output file path. Use .pdf or .svg suffix (default: butil.svg).",
    )
    return parser.parse_args()


def load_csv(filepath: Path) -> dict[str, dict[str, float]]:
    """Return {topology: {series: value}} from a butil CSV file."""
    data: dict[str, dict[str, float]] = {}
    with open(filepath, newline="") as f:
        reader = csv.reader(f)
        header = [h.strip() for h in next(reader)]
        for row in reader:
            cells = [c.strip() for c in row]
            topo = cells[0]
            data[topo] = {header[i]: float(cells[i]) for i in range(1, len(header))}
    return data


def main() -> None:
    args = parse_args()
    csv_dir: Path = args.csv_dir
    if not csv_dir.is_dir():
        print(f"Error: directory '{csv_dir}' not found.", file=sys.stderr)
        sys.exit(1)

    csv_files = sorted(csv_dir.glob("*.csv"))
    if not csv_files:
        print(f"Error: no CSV files in '{csv_dir}'.", file=sys.stderr)
        sys.exit(1)

    n_subplots = len(csv_files)
    x = np.arange(len(TOPOLOGY_ORDER))
    n_bars = len(SERIES_NAMES)
    bar_width = 0.18
    offsets = np.arange(n_bars) * bar_width - (n_bars - 1) * bar_width / 2

    fig, axes = plt.subplots(1, n_subplots, figsize=(3.2 * n_subplots, 3.2), sharey=True)
    if n_subplots == 1:
        axes = [axes]

    global_max = 0.0
    all_data: list[tuple[str, dict[str, dict[str, float]]]] = []
    for fp in csv_files:
        traffic = fp.stem
        data = load_csv(fp)
        all_data.append((traffic, data))
        for topo in TOPOLOGY_ORDER:
            if topo in data:
                for s in SERIES_NAMES:
                    if s in data[topo]:
                        global_max = max(global_max, data[topo][s])

    y_max = np.ceil(global_max / 5) * 5

    for ax, (traffic, data) in zip(axes, all_data):
        for i, series in enumerate(SERIES_NAMES):
            values = [data.get(topo, {}).get(series, 0.0) for topo in TOPOLOGY_ORDER]
            ax.bar(x + offsets[i], values, bar_width, label=series, color=SERIES_COLORS[series])
        ax.set_title(traffic.capitalize(), fontsize=14, fontweight="bold")
        ax.set_xticks(x)
        ax.set_xticklabels(TOPOLOGY_ORDER, fontsize=12)
        ax.set_ylim(0, y_max)
        ax.tick_params(axis="y", labelsize=12)

    handles, labels = axes[0].get_legend_handles_labels()
    fig.legend(
        handles, labels,
        loc="lower center",
        ncol=n_bars,
        fontsize=12,
        frameon=False,
        bbox_to_anchor=(0.5, 0.02),
    )
    fig.text(
        0.5, -0.02,
        "Average Buffer Utilization (\u00d70.00001)",
        ha="center", va="top", fontsize=14,
    )

    fig.tight_layout(rect=[0, 0.12, 1, 1])

    out: Path = args.output
    fmt = out.suffix.lstrip(".").lower()
    if fmt not in ("pdf", "svg"):
        fmt = "svg"
    fig.savefig(out, format=fmt, bbox_inches="tight", pad_inches=0.05)
    print(f"Saved to {out}")


if __name__ == "__main__":
    main()
