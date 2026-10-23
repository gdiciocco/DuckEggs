#!/usr/bin/env python3
"""Run the C++ parity executable and compare its JSON to Python fixtures."""

from __future__ import annotations

import argparse
import json
import math
import re
import subprocess
import sys
from pathlib import Path
from typing import Any


_TRANSLATED_NOTE_PATH = re.compile(
    r"expected\.controllers\[\d+\]\.insights\.note"
)


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser()
    parser.add_argument("--runner", type=Path, required=True)
    parser.add_argument(
        "--fixture",
        type=Path,
        default=Path("tests/fixtures/core-parity.json"),
    )
    parser.add_argument("--tolerance", type=float, default=1e-7)
    parser.add_argument("--max-report", type=int, default=40)
    return parser.parse_args()


def compare(
    expected: Any,
    actual: Any,
    path: str,
    tolerance: float,
    differences: list[str],
) -> None:
    if len(differences) >= 1000:
        return
    # Python remains the immutable Italian reference. DuckEggs deliberately
    # translates only this human-readable insight while comparing every map,
    # ranking, state, and decision field.
    if _TRANSLATED_NOTE_PATH.fullmatch(path):
        return
    if isinstance(expected, bool) or isinstance(actual, bool):
        if expected is not actual:
            differences.append(f"{path}: expected {expected!r}, got {actual!r}")
        return
    if isinstance(expected, (int, float)) and isinstance(actual, (int, float)):
        if not math.isclose(
            float(expected),
            float(actual),
            rel_tol=tolerance,
            abs_tol=tolerance,
        ):
            differences.append(f"{path}: expected {expected!r}, got {actual!r}")
        return
    if expected is None or actual is None:
        if expected is not actual:
            differences.append(f"{path}: expected {expected!r}, got {actual!r}")
        return
    if isinstance(expected, dict) and isinstance(actual, dict):
        expected_keys = set(expected)
        actual_keys = set(actual)
        if expected_keys != actual_keys:
            differences.append(
                f"{path}: keys differ; missing={sorted(expected_keys - actual_keys)}, "
                f"extra={sorted(actual_keys - expected_keys)}"
            )
        for key in sorted(expected_keys & actual_keys):
            compare(
                expected[key],
                actual[key],
                f"{path}.{key}",
                tolerance,
                differences,
            )
        return
    if isinstance(expected, list) and isinstance(actual, list):
        if len(expected) != len(actual):
            differences.append(
                f"{path}: expected {len(expected)} items, got {len(actual)}"
            )
        for index, (expected_item, actual_item) in enumerate(
            zip(expected, actual)
        ):
            compare(
                expected_item,
                actual_item,
                f"{path}[{index}]",
                tolerance,
                differences,
            )
        return
    if expected != actual:
        differences.append(f"{path}: expected {expected!r}, got {actual!r}")


def main() -> int:
    args = parse_args()
    fixture = json.loads(args.fixture.read_text(encoding="utf-8"))
    completed = subprocess.run(
        [str(args.runner), str(args.fixture)],
        check=False,
        capture_output=True,
        text=True,
        timeout=120,
    )
    if completed.returncode != 0:
        sys.stderr.write(completed.stdout)
        sys.stderr.write(completed.stderr)
        print(
            f"parity runner exited with {completed.returncode}",
            file=sys.stderr,
        )
        return completed.returncode or 1
    try:
        actual = json.loads(completed.stdout)
    except json.JSONDecodeError as error:
        print(f"parity runner emitted invalid JSON: {error}", file=sys.stderr)
        return 1

    differences: list[str] = []
    compare(
        fixture["expected"],
        actual,
        "expected",
        args.tolerance,
        differences,
    )
    if differences:
        for difference in differences[: args.max_report]:
            print(difference, file=sys.stderr)
        if len(differences) > args.max_report:
            print(
                f"... {len(differences) - args.max_report} more differences",
                file=sys.stderr,
            )
        print(f"parity failed: {len(differences)} difference(s)", file=sys.stderr)
        return 1
    print(
        "parity passed: "
        f"{', '.join(f'{name}={len(fixture['expected'][name])}' for name in fixture['expected'])}"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
