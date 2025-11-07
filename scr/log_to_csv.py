#!/usr/bin/env python3
from __future__ import annotations

import argparse
import csv
import re
import sys
from collections import defaultdict
from decimal import Decimal, InvalidOperation, ROUND_HALF_UP
from pathlib import Path


LATENCY_PATTERNS = (
    re.compile(r"Packet latency average = (?P<value>\d+(?:\.\d+)?)"),
    re.compile(r"Average latency (?P<value>\d+(?:\.\d+)?)"),
)
LOG_NAME_PATTERN = re.compile(r"rnd_(?P<rate>\d+(?:\.\d+)?)\.log$")
ROUTER_TO_COLUMN = {
    "red": "ReD",
    "ref": "ReD",
    "vda": "VDA",
    "rc": "RC",
    "vcs": "VNS",
    "mvn": "MinVNS",
}
COLUMN_ORDER = ["ReD", "VDA", "RC", "VNS", "MinVNS"]
CSV_HEADER = ["Packet Injection Rate (0.0001 flits/cycle/node)", *COLUMN_ORDER]
INJECTION_RATE_SCALE = Decimal("10000")
LATENCY_PRECISION = Decimal("0.01")


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description=(
            "Convert logs/<traffic>/<topo>/<router>/rnd_<inject_rate>.log into "
            "csv/<traffic>/<topo>.csv latency tables."
        )
    )
    parser.add_argument(
        "--logs-dir",
        "-l",
        default="logs",
        type=Path,
        help="Root directory that contains traffic/topology/router log folders (default: logs).",
    )
    parser.add_argument(
        "--output-dir",
        "-o",
        default="csv",
        type=Path,
        help="Root directory for generated CSV files (default: csv).",
    )
    return parser.parse_args()


def warn(message: str) -> None:
    print(f"Warning: {message}", file=sys.stderr)


def to_text(value: Decimal) -> str:
    text = format(value, "f")
    if "." in text:
        text = text.rstrip("0").rstrip(".")
    return text


def format_latency(value: Decimal) -> str:
    rounded = value.quantize(LATENCY_PRECISION, rounding=ROUND_HALF_UP)
    return to_text(rounded)


def extract_latency(log_path: Path) -> Decimal | None:
    content = log_path.read_text(encoding="utf-8")
    for pattern in LATENCY_PATTERNS:
        match = pattern.search(content)
        if match:
            return Decimal(match.group("value"))
    return None


def parse_rate_units(log_path: Path) -> Decimal | None:
    match = LOG_NAME_PATTERN.match(log_path.name)
    if not match:
        return None
    try:
        return Decimal(match.group("rate")) * INJECTION_RATE_SCALE
    except InvalidOperation:
        return None


def collect_rows(log_root: Path) -> dict[tuple[str, str], dict[Decimal, dict[str, Decimal]]]:
    grouped_rows: dict[tuple[str, str], dict[Decimal, dict[str, Decimal]]] = defaultdict(
        lambda: defaultdict(dict)
    )

    for log_path in sorted(log_root.glob("*/*/*/rnd_*.log")):
        relative_parts = log_path.relative_to(log_root).parts
        if len(relative_parts) != 4:
            warn(f"Skipping unexpected path layout: {log_path}")
            continue

        traffic, topo, router, _ = relative_parts
        column = ROUTER_TO_COLUMN.get(router.lower())
        if column is None:
            warn(f"Skipping unsupported router '{router}' in {log_path}")
            continue

        rate_units = parse_rate_units(log_path)
        if rate_units is None:
            warn(f"Skipping log with unparseable injection rate: {log_path}")
            continue

        latency = extract_latency(log_path)
        if latency is None:
            warn(f"No latency marker found in {log_path}")
            continue

        row = grouped_rows[(traffic, topo)][rate_units]
        existing = row.get(column)
        if existing is not None:
            warn(
                "Duplicate latency entry for "
                f"{traffic}/{topo} rate={to_text(rate_units)} column={column}; "
                f"overwriting {existing} with {latency}"
            )
        row[column] = latency

    return grouped_rows


def write_csv(
    output_root: Path,
    traffic: str,
    topo: str,
    rows_by_rate: dict[Decimal, dict[str, Decimal]],
) -> Path:
    output_path = output_root / traffic / f"{topo}.csv"
    output_path.parent.mkdir(parents=True, exist_ok=True)

    with output_path.open("w", newline="", encoding="utf-8") as csv_file:
        writer = csv.writer(csv_file)
        writer.writerow(CSV_HEADER)
        for rate_units in sorted(rows_by_rate):
            router_values = rows_by_rate[rate_units]
            writer.writerow(
                [
                    to_text(rate_units),
                    *[
                        format_latency(router_values[column])
                        if column in router_values
                        else ""
                        for column in COLUMN_ORDER
                    ],
                ]
            )

    return output_path


def main() -> int:
    args = parse_args()
    log_root = args.logs_dir.resolve()
    output_root = args.output_dir.resolve()

    if not log_root.is_dir():
        print(f"Error: logs directory does not exist: {log_root}", file=sys.stderr)
        return 1

    grouped_rows = collect_rows(log_root)
    if not grouped_rows:
        print(f"Error: no matching log files were found under {log_root}", file=sys.stderr)
        return 1

    written_files = []
    for (traffic, topo), rows_by_rate in sorted(grouped_rows.items()):
        written_files.append(write_csv(output_root, traffic, topo, rows_by_rate))

    for output_path in written_files:
        print(output_path)

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
