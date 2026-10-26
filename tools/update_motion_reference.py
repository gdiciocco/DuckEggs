#!/usr/bin/env python3
"""Re-align controller replay poses with the C++ monotonic-motion revision.

The Python fixtures remain the immutable reference for every non-controller
calculation. This one-time migration replays controller events in growing
prefixes, replacing only PiP poses that followed a prior emitted command or
maintained the prior pose. Deliberate manual PiP changes remain untouched.
"""

from __future__ import annotations

import argparse
import copy
import json
import re
import subprocess
import tempfile
from pathlib import Path
from typing import Any


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser()
    parser.add_argument("--runner", type=Path, required=True)
    parser.add_argument(
        "--fixture",
        type=Path,
        default=Path("tests/fixtures/core-parity.json"),
    )
    return parser.parse_args()


def matching_delimiter(
    text: str, start: int, opening: str, closing: str
) -> int:
    if text[start] != opening:
        raise ValueError(f"expected {opening!r} at offset {start}")
    depth = 0
    in_string = False
    escaped = False
    for offset in range(start, len(text)):
        character = text[offset]
        if in_string:
            if escaped:
                escaped = False
            elif character == "\\":
                escaped = True
            elif character == '"':
                in_string = False
            continue
        if character == '"':
            in_string = True
        elif character == opening:
            depth += 1
        elif character == closing:
            depth -= 1
            if depth == 0:
                return offset
    raise ValueError(f"unterminated {opening!r} at offset {start}")


def top_level_object_span(text: str, key: str) -> tuple[int, int]:
    pattern = re.compile(rf'^  "{re.escape(key)}": \{{', re.MULTILINE)
    matches = list(pattern.finditer(text))
    if len(matches) != 1:
        raise ValueError(f"expected one top-level {key!r} object")
    start = text.index("{", matches[0].start())
    return start, matching_delimiter(text, start, "{", "}")


def controllers_array_span(text: str, top_level_key: str) -> tuple[int, int]:
    object_start, object_end = top_level_object_span(text, top_level_key)
    object_text = text[object_start : object_end + 1]
    pattern = re.compile(r'^    "controllers": \[', re.MULTILINE)
    matches = list(pattern.finditer(object_text))
    if len(matches) != 1:
        raise ValueError(
            f"expected one controllers array in {top_level_key!r}"
        )
    start = object_start + object_text.index("[", matches[0].start())
    return start, matching_delimiter(text, start, "[", "]")


def render_array(value: Any) -> str:
    rendered = json.dumps(
        value, ensure_ascii=False, allow_nan=False, indent=2
    )
    return rendered.replace("\n", "\n    ")


def render_object(value: Any) -> str:
    rendered = json.dumps(
        value, ensure_ascii=False, allow_nan=False, indent=2
    )
    return rendered.replace("\n", "\n    ")


def run_prefix(
    runner: Path,
    fixture: dict[str, Any],
    working_controllers: list[dict[str, Any]],
    prefix: int,
) -> dict[str, Any]:
    probe = copy.deepcopy(fixture)
    probe_controllers = copy.deepcopy(working_controllers)
    for case in probe_controllers:
        case["events"] = case["events"][:prefix]
    probe["input"]["controllers"] = probe_controllers

    with tempfile.TemporaryDirectory(prefix="schiva-motion-replay-") as temp:
        probe_path = Path(temp) / "fixture.json"
        probe_path.write_text(
            json.dumps(probe, ensure_ascii=False, allow_nan=False),
            encoding="utf-8",
        )
        completed = subprocess.run(
            [str(runner), str(probe_path)],
            check=False,
            capture_output=True,
            text=True,
            timeout=120,
        )
    if completed.returncode != 0:
        raise RuntimeError(
            "parity runner failed at prefix "
            f"{prefix}: {completed.stderr.strip()}"
        )
    return json.loads(completed.stdout)


def replay_kind(
    old_case: dict[str, Any],
    old_result: dict[str, Any],
    index: int,
) -> str | None:
    events = old_case["events"]
    decisions = old_result["decisions"]
    if index + 1 >= len(events) or index >= len(decisions):
        return None
    previous_pip = events[index]["observation"].get("pip")
    next_pip = events[index + 1]["observation"].get("pip")
    if previous_pip is None or next_pip is None:
        return None
    previous_rect = previous_pip.get("rect")
    next_rect = next_pip.get("rect")
    if previous_rect is None or next_rect is None:
        return None
    emitted = decisions[index].get("move_to")
    if emitted is not None and next_rect[:2] == emitted:
        return "emitted"
    if next_rect == previous_rect:
        return "maintained"
    return None


def set_event_pip(
    event: dict[str, Any], old_rect: list[int], new_rect: list[int]
) -> None:
    observation = event["observation"]
    pip = observation.get("pip")
    if pip is None:
        return
    pip["rect"] = new_rect
    frame = observation.get("visual_frame")
    if not isinstance(frame, dict):
        return
    excluded = frame.get("excluded")
    if not isinstance(excluded, list):
        return
    frame["excluded"] = [
        new_rect if rectangle == old_rect else rectangle
        for rectangle in excluded
    ]


def main() -> int:
    args = parse_args()
    original_text = args.fixture.read_text(encoding="utf-8")
    fixture = json.loads(original_text)
    input_cases = fixture["input"]["controllers"]
    old_results = fixture["expected"]["controllers"]
    old_by_name = {case["name"]: case for case in old_results}
    if len(old_by_name) != len(old_results):
        raise ValueError("duplicate expected controller names")
    if [case["name"] for case in input_cases] != [
        case["name"] for case in old_results
    ]:
        raise ValueError("controller input and expected order differs")

    working = copy.deepcopy(input_cases)
    maximum_prefix = max(len(case["events"]) for case in input_cases)
    for prefix in range(1, maximum_prefix):
        output = run_prefix(args.runner, fixture, working, prefix)
        output_by_name = {
            case["name"]: case for case in output["controllers"]
        }
        for case_index, old_case in enumerate(input_cases):
            if prefix >= len(old_case["events"]):
                continue
            kind = replay_kind(
                old_case, old_by_name[old_case["name"]], prefix - 1
            )
            if kind is None:
                continue
            actual = output_by_name[old_case["name"]]["decisions"][
                prefix - 1
            ]
            previous_event = working[case_index]["events"][prefix - 1]
            next_event = working[case_index]["events"][prefix]
            previous_rect = previous_event["observation"]["pip"]["rect"]
            old_next_rect = old_case["events"][prefix]["observation"]["pip"][
                "rect"
            ]
            emitted = actual.get("move_to")
            if emitted is None:
                new_rect = list(previous_rect)
            else:
                new_rect = [
                    emitted[0],
                    emitted[1],
                    emitted[0] + previous_rect[2] - previous_rect[0],
                    emitted[1] + previous_rect[3] - previous_rect[1],
                ]
            set_event_pip(next_event, old_next_rect, new_rect)

    final_output = run_prefix(
        args.runner, fixture, working, maximum_prefix
    )
    metadata = copy.deepcopy(fixture.get("metadata", {}))
    metadata["controller_reference_revision"] = {
        "generated_by": "tools/update_motion_reference.py",
        "native_change": (
            "cursor escape uses monotonic clearance while the pointer "
            "threatens the source or current PiP"
        ),
        "python_reference": (
            "all non-controller math, geometry, activity, and visual "
            "fixture sections remain the immutable Python reference"
        ),
        "replay_rule": (
            "PiP poses following a prior emitted command or maintaining "
            "the prior pose were replayed; deliberate manual changes, "
            "cursor positions, buttons, and timing were preserved"
        ),
    }

    expected_start, expected_end = controllers_array_span(
        original_text, "expected"
    )
    input_start, input_end = controllers_array_span(
        original_text, "input"
    )
    metadata_start, metadata_end = top_level_object_span(
        original_text, "metadata"
    )
    replacements = [
        (
            expected_start,
            expected_end + 1,
            render_array(final_output["controllers"]),
        ),
        (
            input_start,
            input_end + 1,
            render_array(working),
        ),
        (
            metadata_start,
            metadata_end + 1,
            render_object(metadata),
        ),
    ]
    updated_text = original_text
    for start, end, replacement in sorted(
        replacements, reverse=True, key=lambda item: item[0]
    ):
        updated_text = (
            updated_text[:start] + replacement + updated_text[end:]
        )
    json.loads(updated_text)
    args.fixture.write_text(updated_text, encoding="utf-8")
    print(
        f"updated {args.fixture} with {len(working)} controller scenarios "
        f"over {maximum_prefix} replay ticks"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
