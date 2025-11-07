#!/usr/bin/env python3
from __future__ import annotations

import argparse
import csv
import math
import sys
from dataclasses import dataclass
from pathlib import Path
from xml.sax.saxutils import escape


TOPOLOGY_ORDER = ["mesh", "p2p", "twin"]
TOPOLOGY_LABELS = {
    "mesh": "Mesh",
    "p2p": "P2P",
    "twin": "Twin",
}
TRAFFIC_LABELS = {
    "bitcomp": "Bit Complement",
    "uniform": "Uniform Random",
}
SERIES_COLORS = [
    "#1f77b4",
    "#ff7f0e",
    "#2ca02c",
    "#d62728",
    "#9467bd",
    "#8c564b",
]
SERIES_MARKERS = ["circle", "square", "triangle", "diamond", "cross", "plus"]
Y_AXIS_MAX = 200.0
CHART_WIDTH = 280
CHART_HEIGHT = 360
ROW_GAP = 64
COL_GAP = 18
FIGURE_PADDING_TOP = 36
FIGURE_PADDING_RIGHT = 28
FIGURE_PADDING_BOTTOM = 96
ROW_LABEL_WIDTH = 60
PLOT_LEFT = 62
PLOT_RIGHT = 18
PLOT_TOP = 38
PLOT_BOTTOM = 96
MARKER_SIZE = 4.5
FONT_FAMILY = "Arial, Helvetica, sans-serif"
AXIS_TITLE_FONT_SIZE = 14
SUBPLOT_TAG_FONT_SIZE = 16


@dataclass(frozen=True)
class SeriesStyle:
    color: str
    marker: str


@dataclass(frozen=True)
class SeriesData:
    name: str
    samples: list[tuple[float, float | None]]


@dataclass(frozen=True)
class ChartData:
    traffic: str
    topo: str
    x_label: str
    x_values: list[float]
    series: list[SeriesData]
    y_min: float


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description=(
            "Convert csv/<traffic>/<topo>.csv latency tables into a combined SVG line chart."
        )
    )
    parser.add_argument(
        "--csv-dir",
        "-c",
        default="csv",
        type=Path,
        help="Root directory that contains traffic folders with CSV files (default: csv).",
    )
    parser.add_argument(
        "--output",
        "-o",
        default="latency.svg",
        type=Path,
        help="Output SVG file path (default: latency.svg).",
    )
    return parser.parse_args()


def parse_number(text: str) -> float | None:
    stripped = text.strip()
    if not stripped:
        return None
    try:
        return float(stripped)
    except ValueError as exc:
        raise ValueError(f"invalid numeric value '{text}'") from exc


def topo_sort_key(topo: str) -> tuple[int, int | str]:
    lowered = topo.lower()
    if lowered in TOPOLOGY_ORDER:
        return (0, TOPOLOGY_ORDER.index(lowered))
    return (1, lowered)


def humanize(text: str) -> str:
    return text.replace("_", " ").replace("-", " ").title()


def traffic_label(text: str) -> str:
    return TRAFFIC_LABELS.get(text.lower(), humanize(text))


def format_number(value: float) -> str:
    if math.isclose(value, round(value), rel_tol=0.0, abs_tol=1e-9):
        return str(int(round(value)))
    return f"{value:.2f}".rstrip("0").rstrip(".")


def round_down_to_ten(value: float) -> int:
    return int(math.floor(value / 10.0) * 10)


def wrap_text(text: str, max_chars: int) -> list[str]:
    words = text.split()
    if not words:
        return [text]

    lines = [words[0]]
    for word in words[1:]:
        candidate = f"{lines[-1]} {word}"
        if len(candidate) <= max_chars:
            lines[-1] = candidate
        else:
            lines.append(word)
    return lines


def subplot_tag(index: int) -> str:
    label = ""
    current = index
    while True:
        current, remainder = divmod(current, 26)
        label = chr(ord("a") + remainder) + label
        if current == 0:
            return f"({label})"
        current -= 1


def select_ticks(values: list[float], max_ticks: int) -> list[float]:
    if not values:
        return []
    if len(values) <= max_ticks:
        return values

    indices = {
        round(index * (len(values) - 1) / (max_ticks - 1))
        for index in range(max_ticks)
    }
    return [values[index] for index in sorted(indices)]


def build_y_axis(y_min: float, y_max: float, max_tick_count: int = 6) -> tuple[int, list[int]]:
    axis_max = int(y_max)
    axis_min = min(round_down_to_ten(y_min), axis_max - 10)
    axis_range = axis_max - axis_min
    step = max(10, int(math.ceil((axis_range / max(1, max_tick_count - 1)) / 10.0) * 10))

    ticks = list(range(axis_min, axis_max + 1, step))
    if ticks[-1] != axis_max:
        ticks.append(axis_max)
    return axis_min, ticks


def map_x(value: float, x_min: float, x_max: float, plot_x: float, plot_width: float) -> float:
    if math.isclose(x_min, x_max, rel_tol=0.0, abs_tol=1e-9):
        return plot_x + (plot_width / 2.0)
    return plot_x + ((value - x_min) / (x_max - x_min)) * plot_width


def map_y(value: float, y_min: float, y_max: float, plot_y: float, plot_height: float) -> float:
    if math.isclose(y_min, y_max, rel_tol=0.0, abs_tol=1e-9):
        return plot_y + (plot_height / 2.0)
    return plot_y + plot_height - ((value - y_min) / (y_max - y_min)) * plot_height


def build_series_styles(series_names: list[str]) -> dict[str, SeriesStyle]:
    styles: dict[str, SeriesStyle] = {}
    for index, series_name in enumerate(series_names):
        styles[series_name] = SeriesStyle(
            color=SERIES_COLORS[index % len(SERIES_COLORS)],
            marker=SERIES_MARKERS[index % len(SERIES_MARKERS)],
        )
    return styles


def read_chart(csv_path: Path, traffic: str, topo: str) -> ChartData:
    with csv_path.open(newline="", encoding="utf-8") as csv_file:
        rows = list(csv.reader(csv_file))

    if not rows:
        raise ValueError(f"{csv_path} is empty")

    header = rows[0]
    if len(header) < 2:
        raise ValueError(f"{csv_path} must contain at least one x-axis column and one series")

    x_label = header[0].strip() or "X Axis"
    series_names = [name.strip() or f"Series {index}" for index, name in enumerate(header[1:], start=1)]
    raw_series: dict[str, list[tuple[float, float | None]]] = {name: [] for name in series_names}
    x_values: list[float] = []
    y_values: list[float] = []

    for line_number, row in enumerate(rows[1:], start=2):
        if not any(cell.strip() for cell in row):
            continue

        x_text = row[0] if row else ""
        x_value = parse_number(x_text)
        if x_value is None:
            raise ValueError(f"{csv_path}:{line_number} has an empty x-axis value")

        x_values.append(x_value)
        for index, series_name in enumerate(series_names, start=1):
            cell = row[index] if index < len(row) else ""
            y_value = parse_number(cell)
            raw_series[series_name].append((x_value, y_value))
            if y_value is not None:
                y_values.append(y_value)

    if not x_values:
        raise ValueError(f"{csv_path} does not contain any data rows")
    if not y_values:
        raise ValueError(f"{csv_path} does not contain any numeric latency values")

    return ChartData(
        traffic=traffic,
        topo=topo,
        x_label=x_label,
        x_values=x_values,
        series=[SeriesData(name=name, samples=raw_series[name]) for name in series_names],
        y_min=min(y_values),
    )


def collect_charts(
    csv_root: Path,
) -> tuple[dict[tuple[str, str], ChartData], list[str], list[str], list[str]]:
    if not csv_root.is_dir():
        raise ValueError(f"CSV directory does not exist: {csv_root}")

    charts: dict[tuple[str, str], ChartData] = {}
    traffic_names: list[str] = []
    topo_names: set[str] = set()
    series_names: list[str] = []

    for traffic_dir in sorted(path for path in csv_root.iterdir() if path.is_dir()):
        traffic = traffic_dir.name
        traffic_names.append(traffic)
        for csv_path in sorted(traffic_dir.glob("*.csv"), key=lambda path: topo_sort_key(path.stem)):
            topo = csv_path.stem
            chart = read_chart(csv_path, traffic, topo)
            charts[(traffic, topo)] = chart
            topo_names.add(topo)
            for series in chart.series:
                if series.name not in series_names:
                    series_names.append(series.name)

    if not charts:
        raise ValueError(f"no CSV files were found under {csv_root}")

    sorted_topologies = sorted(topo_names, key=topo_sort_key)
    return charts, traffic_names, sorted_topologies, series_names


def marker_svg(marker: str, x: float, y: float, color: str, size: float) -> list[str]:
    if marker == "circle":
        return [
            (
                f'<circle cx="{x:.2f}" cy="{y:.2f}" r="{size:.2f}" fill="{color}" '
                f'stroke="{color}" stroke-width="1.2" />'
            )
        ]
    if marker == "square":
        return [
            (
                f'<rect x="{x - size:.2f}" y="{y - size:.2f}" width="{size * 2:.2f}" '
                f'height="{size * 2:.2f}" fill="{color}" stroke="{color}" stroke-width="1.2" />'
            )
        ]
    if marker == "triangle":
        points = [
            (x, y - size * 1.15),
            (x - size, y + size * 0.8),
            (x + size, y + size * 0.8),
        ]
        return [
            f'<polygon points="{" ".join(f"{px:.2f},{py:.2f}" for px, py in points)}" fill="{color}" stroke="{color}" stroke-width="1.2" />'
        ]
    if marker == "diamond":
        points = [
            (x, y - size * 1.2),
            (x - size, y),
            (x, y + size * 1.2),
            (x + size, y),
        ]
        return [
            f'<polygon points="{" ".join(f"{px:.2f},{py:.2f}" for px, py in points)}" fill="{color}" stroke="{color}" stroke-width="1.2" />'
        ]
    if marker == "cross":
        return [
            f'<line x1="{x - size:.2f}" y1="{y - size:.2f}" x2="{x + size:.2f}" y2="{y + size:.2f}" stroke="{color}" stroke-width="1.6" />',
            f'<line x1="{x - size:.2f}" y1="{y + size:.2f}" x2="{x + size:.2f}" y2="{y - size:.2f}" stroke="{color}" stroke-width="1.6" />',
        ]
    return [
        f'<line x1="{x - size:.2f}" y1="{y:.2f}" x2="{x + size:.2f}" y2="{y:.2f}" stroke="{color}" stroke-width="1.6" />',
        f'<line x1="{x:.2f}" y1="{y - size:.2f}" x2="{x:.2f}" y2="{y + size:.2f}" stroke="{color}" stroke-width="1.6" />',
    ]


def split_segments(samples: list[tuple[float, float | None]]) -> list[list[tuple[float, float]]]:
    segments: list[list[tuple[float, float]]] = []
    current_segment: list[tuple[float, float]] = []
    for x_value, y_value in samples:
        if y_value is None:
            if current_segment:
                segments.append(current_segment)
                current_segment = []
            continue
        current_segment.append((x_value, y_value))
    if current_segment:
        segments.append(current_segment)
    return segments


def render_legend(
    elements: list[str],
    series_names: list[str],
    styles: dict[str, SeriesStyle],
    width: float,
    baseline_y: float,
) -> None:
    if not series_names:
        return

    marker_and_line_width = 30.0
    entry_gap = 20.0
    entry_widths = [marker_and_line_width + (len(series_name) * 7.2) for series_name in series_names]
    total_width = sum(entry_widths) + entry_gap * max(0, len(series_names) - 1)
    entry_x = max(16.0, (width - total_width) / 2.0)

    for index, series_name in enumerate(series_names):
        style = styles[series_name]
        elements.append(
            f'<line x1="{entry_x:.2f}" y1="{baseline_y:.2f}" x2="{entry_x + 22:.2f}" y2="{baseline_y:.2f}" '
            f'stroke="{style.color}" stroke-width="2.2" stroke-linecap="round" />'
        )
        elements.extend(marker_svg(style.marker, entry_x + 11, baseline_y, style.color, MARKER_SIZE))
        elements.append(
            f'<text x="{entry_x + 30:.2f}" y="{baseline_y + 4:.2f}" font-size="12" fill="#222">{escape(series_name)}</text>'
        )
        entry_x += entry_widths[index] + entry_gap


def render_chart(
    elements: list[str],
    defs: list[str],
    chart: ChartData,
    styles: dict[str, SeriesStyle],
    left: float,
    top: float,
    clip_id: str,
    tag: str,
) -> None:
    plot_x = left + PLOT_LEFT
    plot_y = top + PLOT_TOP
    plot_width = CHART_WIDTH - PLOT_LEFT - PLOT_RIGHT
    plot_height = CHART_HEIGHT - PLOT_TOP - PLOT_BOTTOM
    x_min = min(chart.x_values)
    x_max = max(chart.x_values)
    y_max = Y_AXIS_MAX
    y_min, y_ticks = build_y_axis(chart.y_min, y_max)
    x_title_lines = wrap_text(chart.x_label, 28)

    title = TOPOLOGY_LABELS.get(chart.topo.lower(), humanize(chart.topo))

    defs.append(
        f'<clipPath id="{clip_id}"><rect x="{plot_x:.2f}" y="{plot_y:.2f}" width="{plot_width:.2f}" height="{plot_height:.2f}" /></clipPath>'
    )
    elements.append(
        f'<rect x="{left:.2f}" y="{top:.2f}" width="{CHART_WIDTH:.2f}" height="{CHART_HEIGHT:.2f}" fill="#ffffff" stroke="#d9d9d9" stroke-width="1" />'
    )
    elements.append(
        f'<text x="{left + CHART_WIDTH / 2:.2f}" y="{top + 22:.2f}" text-anchor="middle" font-size="16" font-weight="bold" fill="#222">{escape(title)}</text>'
    )

    for tick in y_ticks:
        tick_y = map_y(tick, y_min, y_max, plot_y, plot_height)
        elements.append(
            f'<line x1="{plot_x:.2f}" y1="{tick_y:.2f}" x2="{plot_x + plot_width:.2f}" y2="{tick_y:.2f}" stroke="#ececec" stroke-width="1" />'
        )
        elements.append(
            f'<line x1="{plot_x - 5:.2f}" y1="{tick_y:.2f}" x2="{plot_x:.2f}" y2="{tick_y:.2f}" stroke="#333" stroke-width="1" />'
        )
        elements.append(
            f'<text x="{plot_x - 8:.2f}" y="{tick_y + 4:.2f}" text-anchor="end" font-size="11" fill="#444">{escape(format_number(tick))}</text>'
        )

    x_ticks = select_ticks(chart.x_values, 6)
    for tick in x_ticks:
        tick_x = map_x(tick, x_min, x_max, plot_x, plot_width)
        elements.append(
            f'<line x1="{tick_x:.2f}" y1="{plot_y + plot_height:.2f}" x2="{tick_x:.2f}" y2="{plot_y + plot_height + 5:.2f}" stroke="#333" stroke-width="1" />'
        )
        elements.append(
            f'<text x="{tick_x:.2f}" y="{plot_y + plot_height + 20:.2f}" text-anchor="middle" font-size="11" fill="#444">{escape(format_number(tick))}</text>'
        )

    elements.append(
        f'<line x1="{plot_x:.2f}" y1="{plot_y:.2f}" x2="{plot_x:.2f}" y2="{plot_y + plot_height:.2f}" stroke="#333" stroke-width="1.2" />'
    )
    elements.append(
        f'<line x1="{plot_x:.2f}" y1="{plot_y + plot_height:.2f}" x2="{plot_x + plot_width:.2f}" y2="{plot_y + plot_height:.2f}" stroke="#333" stroke-width="1.2" />'
    )
    elements.append(
        f'<text x="{left + 18:.2f}" y="{plot_y + plot_height / 2:.2f}" text-anchor="middle" font-size="{AXIS_TITLE_FONT_SIZE}" fill="#222" transform="rotate(-90 {left + 18:.2f} {plot_y + plot_height / 2:.2f})">Average Packet Latency (cycle)</text>'
    )
    x_title_start_y = plot_y + plot_height + 40
    for line_index, line in enumerate(x_title_lines):
        elements.append(
            f'<text x="{plot_x + plot_width / 2:.2f}" y="{x_title_start_y + line_index * 16:.2f}" text-anchor="middle" font-size="{AXIS_TITLE_FONT_SIZE}" fill="#222">{escape(line)}</text>'
        )
    elements.append(
        f'<text x="{plot_x + plot_width / 2:.2f}" y="{top + CHART_HEIGHT - 12:.2f}" text-anchor="middle" font-size="{SUBPLOT_TAG_FONT_SIZE}" font-weight="bold" fill="#222">{escape(tag)}</text>'
    )

    elements.append(f'<g clip-path="url(#{clip_id})">')
    for series in chart.series:
        style = styles[series.name]
        for segment in split_segments(series.samples):
            if len(segment) >= 2:
                points = " ".join(
                    f"{map_x(x_value, x_min, x_max, plot_x, plot_width):.2f},{map_y(y_value, y_min, y_max, plot_y, plot_height):.2f}"
                    for x_value, y_value in segment
                )
                elements.append(
                    f'<polyline points="{points}" fill="none" stroke="{style.color}" stroke-width="2.1" stroke-linecap="round" stroke-linejoin="round" />'
                )
        for x_value, y_value in series.samples:
            if y_value is None:
                continue
            marker_x = map_x(x_value, x_min, x_max, plot_x, plot_width)
            marker_y = map_y(y_value, y_min, y_max, plot_y, plot_height)
            elements.extend(marker_svg(style.marker, marker_x, marker_y, style.color, MARKER_SIZE))
    elements.append("</g>")


def build_svg(
    charts: dict[tuple[str, str], ChartData],
    traffic_names: list[str],
    topo_names: list[str],
    series_names: list[str],
    styles: dict[str, SeriesStyle],
) -> str:
    grid_width = (CHART_WIDTH * len(topo_names)) + (COL_GAP * max(0, len(topo_names) - 1))
    width = ROW_LABEL_WIDTH + grid_width + FIGURE_PADDING_RIGHT
    height = FIGURE_PADDING_TOP + (CHART_HEIGHT * len(traffic_names)) + (ROW_GAP * max(0, len(traffic_names) - 1)) + FIGURE_PADDING_BOTTOM
    elements = [
        '<?xml version="1.0" encoding="UTF-8"?>',
        (
            f'<svg xmlns="http://www.w3.org/2000/svg" width="{width:.0f}" height="{height:.0f}" '
            f'viewBox="0 0 {width:.0f} {height:.0f}">'
        ),
        (
            "<style>"
            f"text {{ font-family: {FONT_FAMILY}; }}"
            "</style>"
        ),
        f'<rect x="0" y="0" width="{width:.0f}" height="{height:.0f}" fill="#ffffff" />',
    ]
    defs: list[str] = []
    subplot_index = 0

    for row_index, traffic in enumerate(traffic_names):
        row_top = FIGURE_PADDING_TOP + row_index * (CHART_HEIGHT + ROW_GAP)
        row_label = traffic_label(traffic)

        for col_index, topo in enumerate(topo_names):
            chart = charts.get((traffic, topo))
            if chart is None:
                continue
            left = ROW_LABEL_WIDTH + col_index * (CHART_WIDTH + COL_GAP)
            clip_id = f"clip-r{row_index}-c{col_index}"
            render_chart(
                elements,
                defs,
                chart,
                styles,
                left,
                row_top,
                clip_id,
                subplot_tag(subplot_index),
            )
            subplot_index += 1
        elements.append(
            f'<text x="{ROW_LABEL_WIDTH + grid_width / 2:.2f}" y="{row_top + CHART_HEIGHT + 28:.2f}" text-anchor="middle" font-size="16" font-weight="bold" fill="#222">{escape(row_label)}</text>'
        )

    render_legend(elements, series_names, styles, width, height - 34)

    if defs:
        elements.insert(4, f'<defs>{"".join(defs)}</defs>')

    elements.append("</svg>")
    return "\n".join(elements)


def main() -> int:
    args = parse_args()
    csv_root = args.csv_dir.resolve()
    output_path = args.output.resolve()

    try:
        charts, traffic_names, topo_names, series_names = collect_charts(csv_root)
    except ValueError as exc:
        print(f"Error: {exc}", file=sys.stderr)
        return 1

    styles = build_series_styles(series_names)
    svg = build_svg(charts, traffic_names, topo_names, series_names, styles)
    output_path.parent.mkdir(parents=True, exist_ok=True)
    output_path.write_text(svg, encoding="utf-8")
    print(output_path)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
