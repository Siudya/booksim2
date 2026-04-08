#!/usr/bin/env python3
from __future__ import annotations

import argparse
import csv
import math
import sys
import zlib
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
CHART_WIDTH = 240
CHART_HEIGHT = 312
ROW_GAP = 44
COL_GAP = 8
FIGURE_PADDING_TOP = 20
FIGURE_PADDING_RIGHT = 18
FIGURE_PADDING_BOTTOM = 80
ROW_LABEL_WIDTH = 56
PLOT_LEFT = 58
PLOT_RIGHT = 14
PLOT_TOP = 32
PLOT_BOTTOM = 100
ROW_LABEL_OFFSET = 24
MARKER_SIZE = 4.5
FONT_FAMILY = "Arial, Helvetica, sans-serif"
TITLE_FONT_SIZE = 18
TICK_LABEL_FONT_SIZE = 12
AXIS_TITLE_FONT_SIZE = 16
SUBPLOT_TAG_FONT_SIZE = 18
ROW_LABEL_FONT_SIZE = 18
LEGEND_FONT_SIZE = 18
TITLE_BASELINE_OFFSET = 24.0
Y_TICK_LABEL_BASELINE_ADJUST = 4.5
X_TICK_LABEL_OFFSET = 22.0
X_TITLE_OFFSET = 46.0
X_TITLE_LINE_HEIGHT = 18.0
SUBPLOT_TAG_BASELINE_OFFSET = 12.0
LEGEND_MARKER_SIZE = 6.0
LEGEND_LINE_LENGTH = 28.0
LEGEND_TEXT_GAP = 10.0
LEGEND_ENTRY_GAP = 24.0
LEGEND_BASELINE_OFFSET = 30.0
PDF_BASE_SCALE = 0.75
PDF_MAX_DIMENSION = 800.0
PDF_COMPRESS_LEVEL = 9


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


@dataclass(frozen=True)
class FigureLayout:
    width: float
    height: float
    grid_width: float


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description=(
            "Convert csv/<traffic>/<topo>.csv latency tables into a combined SVG or PDF vector line chart."
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
        help="Output SVG or PDF file path (default: latency.svg). Use a .pdf suffix for vector PDF output.",
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


def build_figure_layout(traffic_names: list[str], topo_names: list[str]) -> FigureLayout:
    grid_width = (CHART_WIDTH * len(topo_names)) + (COL_GAP * max(0, len(topo_names) - 1))
    width = ROW_LABEL_WIDTH + grid_width + FIGURE_PADDING_RIGHT
    height = (
        FIGURE_PADDING_TOP
        + (CHART_HEIGHT * len(traffic_names))
        + (ROW_GAP * max(0, len(traffic_names) - 1))
        + FIGURE_PADDING_BOTTOM
    )
    return FigureLayout(width=width, height=height, grid_width=grid_width)


def pdf_scale_for_layout(layout: FigureLayout) -> float:
    longest_side = max(layout.width, layout.height, 1.0)
    return min(PDF_BASE_SCALE, PDF_MAX_DIMENSION / longest_side)


def estimate_text_width(text: str, font_size: float) -> float:
    width_units = 0.0
    for char in text:
        if char == " ":
            width_units += 0.32
        elif char in "ilI1.,:;|'`![]()":
            width_units += 0.28
        elif char in "frt":
            width_units += 0.38
        elif char in "MW@%&Q":
            width_units += 0.86
        elif char.isupper():
            width_units += 0.67
        else:
            width_units += 0.56
    return width_units * font_size


def legend_entry_width(series_name: str) -> float:
    return LEGEND_LINE_LENGTH + LEGEND_TEXT_GAP + estimate_text_width(series_name, LEGEND_FONT_SIZE)


def compact_float(value: float) -> str:
    if math.isclose(value, round(value), rel_tol=0.0, abs_tol=1e-9):
        return str(int(round(value)))
    return f"{value:.3f}".rstrip("0").rstrip(".")


def hex_to_rgb(color: str) -> tuple[float, float, float]:
    if not color.startswith("#"):
        raise ValueError(f"unsupported color value: {color}")
    if len(color) == 4:
        color = "#" + "".join(component * 2 for component in color[1:])
    if len(color) != 7:
        raise ValueError(f"unsupported color value: {color}")
    return (
        int(color[1:3], 16) / 255.0,
        int(color[3:5], 16) / 255.0,
        int(color[5:7], 16) / 255.0,
    )


def pdf_escape_text(text: str) -> str:
    return text.replace("\\", "\\\\").replace("(", "\\(").replace(")", "\\)")


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


def render_legend_svg(
    elements: list[str],
    series_names: list[str],
    styles: dict[str, SeriesStyle],
    width: float,
    baseline_y: float,
) -> None:
    if not series_names:
        return

    entry_widths = [legend_entry_width(series_name) for series_name in series_names]
    total_width = sum(entry_widths) + LEGEND_ENTRY_GAP * max(0, len(series_names) - 1)
    entry_x = max(16.0, (width - total_width) / 2.0)
    text_baseline_y = baseline_y + (LEGEND_FONT_SIZE * 0.32)

    for index, series_name in enumerate(series_names):
        style = styles[series_name]
        elements.append(
            f'<line x1="{entry_x:.2f}" y1="{baseline_y:.2f}" x2="{entry_x + LEGEND_LINE_LENGTH:.2f}" y2="{baseline_y:.2f}" '
            f'stroke="{style.color}" stroke-width="2.4" stroke-linecap="round" />'
        )
        elements.extend(
            marker_svg(style.marker, entry_x + (LEGEND_LINE_LENGTH / 2.0), baseline_y, style.color, LEGEND_MARKER_SIZE)
        )
        elements.append(
            f'<text x="{entry_x + LEGEND_LINE_LENGTH + LEGEND_TEXT_GAP:.2f}" y="{text_baseline_y:.2f}" '
            f'font-size="{LEGEND_FONT_SIZE}" fill="#222">{escape(series_name)}</text>'
        )
        entry_x += entry_widths[index] + LEGEND_ENTRY_GAP


class PdfCanvas:
    def __init__(self, width: float, height: float, scale: float = PDF_BASE_SCALE) -> None:
        self.scale = scale
        self.width = width * scale
        self.height = height * scale
        self.commands: list[str] = []

    def _scale(self, value: float) -> float:
        return value * self.scale

    def _point(self, x: float, y: float) -> tuple[float, float]:
        return self._scale(x), self.height - self._scale(y)

    def _rgb_operands(self, color: str) -> str:
        return " ".join(compact_float(channel) for channel in hex_to_rgb(color))

    def rect(
        self,
        x: float,
        y: float,
        width: float,
        height: float,
        *,
        fill: str | None = None,
        stroke: str | None = None,
        stroke_width: float = 1.0,
    ) -> None:
        left = self._scale(x)
        bottom = self.height - self._scale(y + height)
        rect_width = self._scale(width)
        rect_height = self._scale(height)
        commands: list[str] = []
        if fill is not None:
            commands.append(f"{self._rgb_operands(fill)} rg")
        if stroke is not None:
            commands.append(f"{self._scale(stroke_width):.3f}".rstrip("0").rstrip(".") + " w")
            commands.append(f"{self._rgb_operands(stroke)} RG")
        operator = "B" if fill is not None and stroke is not None else "f" if fill is not None else "S"
        commands.append(
            f"{compact_float(left)} {compact_float(bottom)} {compact_float(rect_width)} {compact_float(rect_height)} re {operator}"
        )
        self.commands.append(" ".join(commands))

    def line(
        self,
        x1: float,
        y1: float,
        x2: float,
        y2: float,
        *,
        color: str,
        stroke_width: float,
        line_cap: int = 0,
    ) -> None:
        start_x, start_y = self._point(x1, y1)
        end_x, end_y = self._point(x2, y2)
        self.commands.append(
            " ".join(
                [
                    f"{compact_float(self._scale(stroke_width))} w",
                    f"{line_cap} J",
                    f"{self._rgb_operands(color)} RG",
                    f"{compact_float(start_x)} {compact_float(start_y)} m {compact_float(end_x)} {compact_float(end_y)} l S",
                ]
            )
        )

    def polyline(
        self,
        points: list[tuple[float, float]],
        *,
        color: str,
        stroke_width: float,
        line_cap: int = 1,
        line_join: int = 1,
    ) -> None:
        if len(points) < 2:
            return
        start_x, start_y = self._point(*points[0])
        path_commands = [f"{compact_float(start_x)} {compact_float(start_y)} m"]
        for x_value, y_value in points[1:]:
            point_x, point_y = self._point(x_value, y_value)
            path_commands.append(f"{compact_float(point_x)} {compact_float(point_y)} l")
        self.commands.append(
            " ".join(
                [
                    f"{compact_float(self._scale(stroke_width))} w",
                    f"{line_cap} J",
                    f"{line_join} j",
                    f"{self._rgb_operands(color)} RG",
                    " ".join(path_commands),
                    "S",
                ]
            )
        )

    def polygon(
        self,
        points: list[tuple[float, float]],
        *,
        fill: str | None = None,
        stroke: str | None = None,
        stroke_width: float = 1.0,
        line_join: int = 1,
    ) -> None:
        if not points:
            return
        start_x, start_y = self._point(*points[0])
        path_commands = [f"{compact_float(start_x)} {compact_float(start_y)} m"]
        for x_value, y_value in points[1:]:
            point_x, point_y = self._point(x_value, y_value)
            path_commands.append(f"{compact_float(point_x)} {compact_float(point_y)} l")
        path_commands.append("h")
        commands: list[str] = [f"{line_join} j"]
        if fill is not None:
            commands.append(f"{self._rgb_operands(fill)} rg")
        if stroke is not None:
            commands.append(f"{compact_float(self._scale(stroke_width))} w")
            commands.append(f"{self._rgb_operands(stroke)} RG")
        operator = "B" if fill is not None and stroke is not None else "f" if fill is not None else "S"
        self.commands.append(" ".join(commands + [" ".join(path_commands), operator]))

    def circle(
        self,
        x: float,
        y: float,
        radius: float,
        *,
        fill: str | None = None,
        stroke: str | None = None,
        stroke_width: float = 1.0,
    ) -> None:
        center_x, center_y = self._point(x, y)
        radius_value = self._scale(radius)
        curve_offset = radius_value * 0.552284749831
        path = [
            f"{compact_float(center_x + radius_value)} {compact_float(center_y)} m",
            (
                f"{compact_float(center_x + radius_value)} {compact_float(center_y + curve_offset)} "
                f"{compact_float(center_x + curve_offset)} {compact_float(center_y + radius_value)} "
                f"{compact_float(center_x)} {compact_float(center_y + radius_value)} c"
            ),
            (
                f"{compact_float(center_x - curve_offset)} {compact_float(center_y + radius_value)} "
                f"{compact_float(center_x - radius_value)} {compact_float(center_y + curve_offset)} "
                f"{compact_float(center_x - radius_value)} {compact_float(center_y)} c"
            ),
            (
                f"{compact_float(center_x - radius_value)} {compact_float(center_y - curve_offset)} "
                f"{compact_float(center_x - curve_offset)} {compact_float(center_y - radius_value)} "
                f"{compact_float(center_x)} {compact_float(center_y - radius_value)} c"
            ),
            (
                f"{compact_float(center_x + curve_offset)} {compact_float(center_y - radius_value)} "
                f"{compact_float(center_x + radius_value)} {compact_float(center_y - curve_offset)} "
                f"{compact_float(center_x + radius_value)} {compact_float(center_y)} c"
            ),
            "h",
        ]
        commands: list[str] = []
        if fill is not None:
            commands.append(f"{self._rgb_operands(fill)} rg")
        if stroke is not None:
            commands.append(f"{compact_float(self._scale(stroke_width))} w")
            commands.append(f"{self._rgb_operands(stroke)} RG")
        operator = "B" if fill is not None and stroke is not None else "f" if fill is not None else "S"
        self.commands.append(" ".join(commands + [" ".join(path), operator]))

    def text(
        self,
        x: float,
        y: float,
        text: str,
        *,
        font_size: float,
        color: str,
        anchor: str = "start",
        bold: bool = False,
        rotation: int = 0,
    ) -> None:
        font_key = "F2" if bold else "F1"
        scaled_font_size = self._scale(font_size)
        text_width = self._scale(estimate_text_width(text, font_size))
        point_x, point_y = self._point(x, y)
        if rotation == 90:
            if anchor == "middle":
                point_y -= text_width / 2.0
            elif anchor == "end":
                point_y -= text_width
            matrix = f"0 1 -1 0 {compact_float(point_x)} {compact_float(point_y)} Tm"
        else:
            if anchor == "middle":
                point_x -= text_width / 2.0
            elif anchor == "end":
                point_x -= text_width
            matrix = f"1 0 0 1 {compact_float(point_x)} {compact_float(point_y)} Tm"
        self.commands.append(
            f"BT /{font_key} {compact_float(scaled_font_size)} Tf {self._rgb_operands(color)} rg {matrix} ({pdf_escape_text(text)}) Tj ET"
        )

    def push_state(self) -> None:
        self.commands.append("q")

    def pop_state(self) -> None:
        self.commands.append("Q")

    def clip_rect(self, x: float, y: float, width: float, height: float) -> None:
        left = self._scale(x)
        bottom = self.height - self._scale(y + height)
        self.commands.append(
            f"{compact_float(left)} {compact_float(bottom)} {compact_float(self._scale(width))} {compact_float(self._scale(height))} re W n"
        )

    def content_stream(self) -> str:
        return "\n".join(self.commands) + "\n"


def marker_pdf(
    canvas: PdfCanvas,
    marker: str,
    x: float,
    y: float,
    color: str,
    size: float,
) -> None:
    if marker == "circle":
        canvas.circle(x, y, size, fill=color, stroke=color, stroke_width=1.2)
        return
    if marker == "square":
        canvas.rect(x - size, y - size, size * 2.0, size * 2.0, fill=color, stroke=color, stroke_width=1.2)
        return
    if marker == "triangle":
        canvas.polygon(
            [
                (x, y - size * 1.15),
                (x - size, y + size * 0.8),
                (x + size, y + size * 0.8),
            ],
            fill=color,
            stroke=color,
            stroke_width=1.2,
        )
        return
    if marker == "diamond":
        canvas.polygon(
            [
                (x, y - size * 1.2),
                (x - size, y),
                (x, y + size * 1.2),
                (x + size, y),
            ],
            fill=color,
            stroke=color,
            stroke_width=1.2,
        )
        return
    if marker == "cross":
        canvas.line(x - size, y - size, x + size, y + size, color=color, stroke_width=1.6, line_cap=1)
        canvas.line(x - size, y + size, x + size, y - size, color=color, stroke_width=1.6, line_cap=1)
        return
    canvas.line(x - size, y, x + size, y, color=color, stroke_width=1.6, line_cap=1)
    canvas.line(x, y - size, x, y + size, color=color, stroke_width=1.6, line_cap=1)


def render_legend_pdf(
    canvas: PdfCanvas,
    series_names: list[str],
    styles: dict[str, SeriesStyle],
    width: float,
    baseline_y: float,
) -> None:
    if not series_names:
        return

    entry_widths = [legend_entry_width(series_name) for series_name in series_names]
    total_width = sum(entry_widths) + LEGEND_ENTRY_GAP * max(0, len(series_names) - 1)
    entry_x = max(16.0, (width - total_width) / 2.0)
    text_baseline_y = baseline_y + (LEGEND_FONT_SIZE * 0.32)

    for index, series_name in enumerate(series_names):
        style = styles[series_name]
        canvas.line(
            entry_x,
            baseline_y,
            entry_x + LEGEND_LINE_LENGTH,
            baseline_y,
            color=style.color,
            stroke_width=2.4,
            line_cap=1,
        )
        marker_pdf(
            canvas,
            style.marker,
            entry_x + (LEGEND_LINE_LENGTH / 2.0),
            baseline_y,
            style.color,
            LEGEND_MARKER_SIZE,
        )
        canvas.text(
            entry_x + LEGEND_LINE_LENGTH + LEGEND_TEXT_GAP,
            text_baseline_y,
            series_name,
            font_size=LEGEND_FONT_SIZE,
            color="#222",
        )
        entry_x += entry_widths[index] + LEGEND_ENTRY_GAP


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
        f'<text x="{left + CHART_WIDTH / 2:.2f}" y="{top + TITLE_BASELINE_OFFSET:.2f}" text-anchor="middle" font-size="{TITLE_FONT_SIZE}" font-weight="bold" fill="#222">{escape(title)}</text>'
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
            f'<text x="{plot_x - 8:.2f}" y="{tick_y + Y_TICK_LABEL_BASELINE_ADJUST:.2f}" text-anchor="end" font-size="{TICK_LABEL_FONT_SIZE}" fill="#444">{escape(format_number(tick))}</text>'
        )

    x_ticks = select_ticks(chart.x_values, 6)
    for tick in x_ticks:
        tick_x = map_x(tick, x_min, x_max, plot_x, plot_width)
        elements.append(
            f'<line x1="{tick_x:.2f}" y1="{plot_y + plot_height:.2f}" x2="{tick_x:.2f}" y2="{plot_y + plot_height + 5:.2f}" stroke="#333" stroke-width="1" />'
        )
        elements.append(
            f'<text x="{tick_x:.2f}" y="{plot_y + plot_height + X_TICK_LABEL_OFFSET:.2f}" text-anchor="middle" font-size="{TICK_LABEL_FONT_SIZE}" fill="#444">{escape(format_number(tick))}</text>'
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
    x_title_start_y = plot_y + plot_height + X_TITLE_OFFSET
    for line_index, line in enumerate(x_title_lines):
        elements.append(
            f'<text x="{plot_x + plot_width / 2:.2f}" y="{x_title_start_y + line_index * X_TITLE_LINE_HEIGHT:.2f}" text-anchor="middle" font-size="{AXIS_TITLE_FONT_SIZE}" fill="#222">{escape(line)}</text>'
        )
    elements.append(
        f'<text x="{plot_x + plot_width / 2:.2f}" y="{top + CHART_HEIGHT - SUBPLOT_TAG_BASELINE_OFFSET:.2f}" text-anchor="middle" font-size="{SUBPLOT_TAG_FONT_SIZE}" font-weight="bold" fill="#222">{escape(tag)}</text>'
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


def render_chart_pdf(
    canvas: PdfCanvas,
    chart: ChartData,
    styles: dict[str, SeriesStyle],
    left: float,
    top: float,
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

    canvas.rect(left, top, CHART_WIDTH, CHART_HEIGHT, fill="#ffffff", stroke="#d9d9d9", stroke_width=1.0)
    canvas.text(
        left + (CHART_WIDTH / 2.0),
        top + TITLE_BASELINE_OFFSET,
        title,
        font_size=TITLE_FONT_SIZE,
        color="#222",
        anchor="middle",
        bold=True,
    )

    for tick in y_ticks:
        tick_y = map_y(tick, y_min, y_max, plot_y, plot_height)
        canvas.line(plot_x, tick_y, plot_x + plot_width, tick_y, color="#ececec", stroke_width=1.0)
        canvas.line(plot_x - 5.0, tick_y, plot_x, tick_y, color="#333", stroke_width=1.0)
        canvas.text(
            plot_x - 8.0,
            tick_y + Y_TICK_LABEL_BASELINE_ADJUST,
            format_number(tick),
            font_size=TICK_LABEL_FONT_SIZE,
            color="#444",
            anchor="end",
        )

    x_ticks = select_ticks(chart.x_values, 6)
    for tick in x_ticks:
        tick_x = map_x(tick, x_min, x_max, plot_x, plot_width)
        canvas.line(
            tick_x,
            plot_y + plot_height,
            tick_x,
            plot_y + plot_height + 5.0,
            color="#333",
            stroke_width=1.0,
        )
        canvas.text(
            tick_x,
            plot_y + plot_height + X_TICK_LABEL_OFFSET,
            format_number(tick),
            font_size=TICK_LABEL_FONT_SIZE,
            color="#444",
            anchor="middle",
        )

    canvas.line(plot_x, plot_y, plot_x, plot_y + plot_height, color="#333", stroke_width=1.2)
    canvas.line(
        plot_x,
        plot_y + plot_height,
        plot_x + plot_width,
        plot_y + plot_height,
        color="#333",
        stroke_width=1.2,
    )
    canvas.text(
        left + 18.0,
        plot_y + (plot_height / 2.0),
        "Average Packet Latency (cycle)",
        font_size=AXIS_TITLE_FONT_SIZE,
        color="#222",
        anchor="middle",
        rotation=90,
    )

    x_title_start_y = plot_y + plot_height + X_TITLE_OFFSET
    for line_index, line in enumerate(x_title_lines):
        canvas.text(
            plot_x + (plot_width / 2.0),
            x_title_start_y + (line_index * X_TITLE_LINE_HEIGHT),
            line,
            font_size=AXIS_TITLE_FONT_SIZE,
            color="#222",
            anchor="middle",
        )
    canvas.text(
        plot_x + (plot_width / 2.0),
        top + CHART_HEIGHT - SUBPLOT_TAG_BASELINE_OFFSET,
        tag,
        font_size=SUBPLOT_TAG_FONT_SIZE,
        color="#222",
        anchor="middle",
        bold=True,
    )

    canvas.push_state()
    canvas.clip_rect(plot_x, plot_y, plot_width, plot_height)
    for series in chart.series:
        style = styles[series.name]
        for segment in split_segments(series.samples):
            if len(segment) >= 2:
                points = [
                    (
                        map_x(x_value, x_min, x_max, plot_x, plot_width),
                        map_y(y_value, y_min, y_max, plot_y, plot_height),
                    )
                    for x_value, y_value in segment
                ]
                canvas.polyline(points, color=style.color, stroke_width=2.1)
        for x_value, y_value in series.samples:
            if y_value is None:
                continue
            marker_pdf(
                canvas,
                style.marker,
                map_x(x_value, x_min, x_max, plot_x, plot_width),
                map_y(y_value, y_min, y_max, plot_y, plot_height),
                style.color,
                MARKER_SIZE,
            )
    canvas.pop_state()


def build_svg(
    charts: dict[tuple[str, str], ChartData],
    traffic_names: list[str],
    topo_names: list[str],
    series_names: list[str],
    styles: dict[str, SeriesStyle],
) -> str:
    layout = build_figure_layout(traffic_names, topo_names)
    width = layout.width
    height = layout.height
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
            f'<text x="{ROW_LABEL_WIDTH + layout.grid_width / 2:.2f}" y="{row_top + CHART_HEIGHT + ROW_LABEL_OFFSET:.2f}" text-anchor="middle" font-size="{ROW_LABEL_FONT_SIZE}" font-weight="bold" fill="#222">{escape(row_label)}</text>'
        )

    render_legend_svg(elements, series_names, styles, width, height - LEGEND_BASELINE_OFFSET)

    if defs:
        elements.insert(4, f'<defs>{"".join(defs)}</defs>')

    elements.append("</svg>")
    return "\n".join(elements)


def build_pdf_document(content_stream: str, width: float, height: float) -> bytes:
    content_bytes = content_stream.encode("latin-1")
    compressed_stream = zlib.compress(content_bytes, level=PDF_COMPRESS_LEVEL)
    stream_object = bytearray()
    stream_object.extend(
        f"<< /Length {len(compressed_stream)} /Filter /FlateDecode >>\nstream\n".encode("ascii")
    )
    stream_object.extend(compressed_stream)
    stream_object.extend(b"\nendstream")

    objects: list[bytes] = [
        b"<< /Type /Catalog /Pages 2 0 R >>",
        b"<< /Type /Pages /Count 1 /Kids [3 0 R] >>",
        (
            f"<< /Type /Page /Parent 2 0 R /MediaBox [0 0 {compact_float(width)} {compact_float(height)}] "
            f"/Resources << /Font << /F1 5 0 R /F2 6 0 R >> >> /Contents 4 0 R >>"
        ).encode("ascii"),
        bytes(stream_object),
        b"<< /Type /Font /Subtype /Type1 /BaseFont /Helvetica /Encoding /WinAnsiEncoding >>",
        b"<< /Type /Font /Subtype /Type1 /BaseFont /Helvetica-Bold /Encoding /WinAnsiEncoding >>",
    ]

    document = bytearray(b"%PDF-1.4\n%\xe2\xe3\xcf\xd3\n")
    offsets = [0]
    for object_index, pdf_object in enumerate(objects, start=1):
        offsets.append(len(document))
        document.extend(f"{object_index} 0 obj\n".encode("ascii"))
        document.extend(pdf_object)
        document.extend(b"\nendobj\n")

    xref_offset = len(document)
    document.extend(f"xref\n0 {len(objects) + 1}\n".encode("ascii"))
    document.extend(b"0000000000 65535 f \n")
    for offset in offsets[1:]:
        document.extend(f"{offset:010d} 00000 n \n".encode("ascii"))
    document.extend(
        f"trailer << /Size {len(objects) + 1} /Root 1 0 R >>\nstartxref\n{xref_offset}\n%%EOF\n".encode(
            "ascii"
        )
    )
    return bytes(document)


def build_pdf(
    charts: dict[tuple[str, str], ChartData],
    traffic_names: list[str],
    topo_names: list[str],
    series_names: list[str],
    styles: dict[str, SeriesStyle],
) -> bytes:
    layout = build_figure_layout(traffic_names, topo_names)
    canvas = PdfCanvas(layout.width, layout.height, scale=pdf_scale_for_layout(layout))
    canvas.rect(0.0, 0.0, layout.width, layout.height, fill="#ffffff")
    subplot_index = 0

    for row_index, traffic in enumerate(traffic_names):
        row_top = FIGURE_PADDING_TOP + row_index * (CHART_HEIGHT + ROW_GAP)
        row_label = traffic_label(traffic)

        for col_index, topo in enumerate(topo_names):
            chart = charts.get((traffic, topo))
            if chart is None:
                continue
            left = ROW_LABEL_WIDTH + col_index * (CHART_WIDTH + COL_GAP)
            render_chart_pdf(
                canvas,
                chart,
                styles,
                left,
                row_top,
                subplot_tag(subplot_index),
            )
            subplot_index += 1
        canvas.text(
            ROW_LABEL_WIDTH + (layout.grid_width / 2.0),
            row_top + CHART_HEIGHT + ROW_LABEL_OFFSET,
            row_label,
            font_size=ROW_LABEL_FONT_SIZE,
            color="#222",
            anchor="middle",
            bold=True,
        )

    render_legend_pdf(canvas, series_names, styles, layout.width, layout.height - LEGEND_BASELINE_OFFSET)
    return build_pdf_document(canvas.content_stream(), canvas.width, canvas.height)


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
    output_path.parent.mkdir(parents=True, exist_ok=True)
    if output_path.suffix.lower() == ".pdf":
        output_path.write_bytes(build_pdf(charts, traffic_names, topo_names, series_names, styles))
    else:
        output_path.write_text(
            build_svg(charts, traffic_names, topo_names, series_names, styles),
            encoding="utf-8",
        )
    print(output_path)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
