#!/usr/bin/env python3
"""Export deterministic Python 0.3.0 core fixtures for C++ parity checks."""

from __future__ import annotations

import argparse
import json
import os
import subprocess
import sys
import tempfile
from pathlib import Path
from typing import Any


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser()
    parser.add_argument(
        "--reference",
        type=Path,
        default=Path(
            os.environ.get(
                "SCHIVA_PYTHON_REFERENCE", "/home/deffie/schiva-adaptive"
            )
        ),
    )
    parser.add_argument(
        "--output",
        type=Path,
        default=Path("tests/fixtures/core-parity.json"),
    )
    return parser.parse_args()


def rect(value: Any) -> list[int]:
    return [value.left, value.top, value.right, value.bottom]


def window(value: Any) -> dict[str, Any]:
    return {
        "window_id": value.window_id,
        "app_id": value.app_id,
        "role": value.role,
        "rect": rect(value.rect),
    }


def visual_frame(value: Any) -> dict[str, Any]:
    return {
        "width": value.width,
        "height": value.height,
        "bounds": rect(value.bounds),
        "pixels": list(value.pixels),
        "excluded": [rect(item) for item in value.excluded],
    }


def frame_spec(
    bounds: Any,
    width: int,
    height: int,
    base: int,
    patches: list[tuple[Any, int]],
    excluded: tuple[Any, ...] = (),
) -> dict[str, Any]:
    return {
        "width": width,
        "height": height,
        "bounds": rect(bounds),
        "base": base,
        "patches": [
            {"rect": rect(patch), "level": level}
            for patch, level in patches
        ],
        "excluded": [rect(item) for item in excluded],
    }


def observation_spec(value: Any) -> dict[str, Any]:
    result: dict[str, Any] = {
        "cursor": list(value.cursor),
        "buttons_down": value.buttons_down,
        "pip": window(value.pip) if value.pip is not None else None,
        "foreground": (
            window(value.foreground)
            if value.foreground is not None
            else None
        ),
        "work_area": rect(value.work_area),
        "windows": [window(item) for item in value.windows],
        "visual_frame": (
            visual_frame(value.visual_frame)
            if value.visual_frame is not None
            else None
        ),
        "caret": rect(value.caret) if value.caret is not None else None,
        "input_active": value.input_active,
        "input_age": value.input_age,
        "interaction_exclusions": [
            rect(item) for item in value.interaction_exclusions
        ],
    }
    return result


def decision_spec(value: Any) -> dict[str, Any]:
    return {
        "state": value.state,
        "move_to": list(value.move_to) if value.move_to is not None else None,
        "pip_id": value.pip_id,
        "reason": value.reason,
        "recommendation": (
            rect(value.recommendation)
            if value.recommendation is not None
            else None
        ),
        "context_key": value.context_key,
        "learning_count": value.learning_count,
    }


def reference_head(reference: Path) -> str | None:
    try:
        return subprocess.check_output(
            ["git", "-C", str(reference), "rev-parse", "HEAD"],
            text=True,
            stderr=subprocess.DEVNULL,
        ).strip()
    except (OSError, subprocess.CalledProcessError):
        return None


def main() -> int:
    args = parse_args()
    source = (args.reference / "src").resolve()
    if not (source / "schiva/core/controller.py").is_file():
        raise SystemExit(f"Python reference not found: {args.reference}")
    sys.path.insert(0, str(source))

    from schiva.core.activity import ActivityMap, InteractionTracker
    from schiva.core.controller import AdaptiveController
    from schiva.core.geometry import (
        MotionTrajectory,
        candidate_rects,
        first_safe_step,
        plan_safe_path,
    )
    from schiva.core.linucb import LinUCBModel
    from schiva.core.storage import LearningStore
    from schiva.core.types import (
        Observation,
        Rect,
        VisualFrame,
        WindowInfo,
    )
    from schiva.core.visual import VisualActivityTracker

    linucb_cases = [
        {
            "name": "known_sherman_morrison",
            "dimension": 3,
            "alpha": 0.0,
            "updates": [
                {"features": [1.0, 0.0, 0.0], "reward": 1.0},
                {"features": [1.0, 0.0, 0.0], "reward": -0.25},
            ],
            "queries": [
                [1.0, 0.0, 0.0],
                [0.0, 0.5, -0.5],
            ],
        },
        {
            "name": "rank_changes_with_reward",
            "dimension": 4,
            "alpha": 0.2,
            "updates": [
                {"features": [1.0, 1.0, 0.0, 0.0], "reward": 0.9},
                {"features": [1.0, -1.0, 0.0, 0.0], "reward": -0.4},
            ],
            "queries": [
                [1.0, 1.0, 0.0, 0.0],
                [1.0, -1.0, 0.0, 0.0],
                [1.0, 0.0, 0.5, -0.5],
            ],
        },
    ]
    linucb_expected = []
    for case in linucb_cases:
        model = LinUCBModel(case["dimension"], alpha=case["alpha"])
        for update in case["updates"]:
            model.update(update["features"], update["reward"])
        linucb_expected.append(
            {
                "name": case["name"],
                "state": model.state(),
                "queries": [
                    {
                        "predict": model.predict(query),
                        "uncertainty": model.uncertainty(query),
                        "score": model.score(query),
                    }
                    for query in case["queries"]
                ],
            }
        )

    geometry_cases = [
        {
            "name": "candidate_edges",
            "operation": "candidates",
            "work": [0, 0, 1600, 900],
            "size": [320, 180],
            "cursor": [640, 450],
            "current": [100, 100, 420, 280],
        },
        {
            "name": "pointer_detour",
            "operation": "path",
            "start": [100, 400],
            "target": [700, 420],
            "size": [300, 200],
            "cursor": [450, 500],
            "safety_margin": 28,
            "work": [0, 0, 1200, 800],
        },
        {
            "name": "bounded_first_step",
            "operation": "first_step",
            "start": [100, 400],
            "target": [700, 420],
            "size": [300, 200],
            "cursor": [450, 500],
            "safety_margin": 28,
            "work": [0, 0, 1200, 800],
            "max_step": 96,
        },
        {
            "name": "smoothstep_120hz",
            "operation": "trajectory",
            "path": [[100, 100], [400, 100], [400, 500]],
            "steps": [1.0 / 120.0] * 72,
        },
        {
            "name": "negative_workspace_path",
            "operation": "path",
            "start": [-1820, -980],
            "target": [-300, -300],
            "size": [320, 180],
            "cursor": [-1410, -830],
            "safety_margin": 28,
            "work": [-1920, -1080, 0, 0],
        },
    ]
    geometry_expected = []
    for case in geometry_cases:
        operation = case["operation"]
        if operation == "candidates":
            value = [
                rect(item)
                for item in candidate_rects(
                    Rect(*case["work"]),
                    tuple(case["size"]),
                    cursor=tuple(case["cursor"]),
                    current=Rect(*case["current"]),
                )
            ]
        elif operation == "path":
            path = plan_safe_path(
                tuple(case["start"]),
                tuple(case["target"]),
                tuple(case["size"]),
                tuple(case["cursor"]),
                safety_margin=case["safety_margin"],
                work=Rect(*case["work"]),
            )
            value = [list(item) for item in path] if path is not None else None
        elif operation == "first_step":
            step = first_safe_step(
                tuple(case["start"]),
                tuple(case["target"]),
                tuple(case["size"]),
                tuple(case["cursor"]),
                safety_margin=case["safety_margin"],
                work=Rect(*case["work"]),
                max_step=case["max_step"],
            )
            value = list(step) if step is not None else None
        else:
            trajectory = MotionTrajectory.from_path(
                [tuple(item) for item in case["path"]]
            )
            positions = [
                list(trajectory.advance(float(step)))
                for step in case["steps"]
            ]
            value = {
                "duration": trajectory.duration,
                "positions": positions,
                "target": list(trajectory.target),
                "complete": trajectory.complete,
            }
        geometry_expected.append({"name": case["name"], "value": value})

    map = ActivityMap(columns=10, rows=7, half_life=2.0)
    map_operations = [
        {
            "op": "add",
            "point": [1.0, 1.0],
            "reference": [0, 0, 240, 140],
            "weight": 2.0,
        },
        {
            "op": "add",
            "point": [121.0, 81.0],
            "reference": [0, 0, 240, 140],
            "weight": 5.0,
        },
        {"op": "decay", "elapsed": 2.0},
    ]
    for operation in map_operations:
        if operation["op"] == "add":
            map.add(
                tuple(operation["point"]),
                Rect(*operation["reference"]),
                operation["weight"],
            )
        else:
            map.decay(operation["elapsed"])
    density_inputs = [
        {"target": [0, 0, 240, 140], "reference": [0, 0, 240, 140]},
        {"target": [0, 0, 48, 40], "reference": [0, 0, 240, 140]},
        {"target": [96, 60, 168, 120], "reference": [0, 0, 240, 140]},
    ]
    tracker = InteractionTracker()
    tracker_observations = []
    work = Rect(0, 0, 240, 140)
    foreground = WindowInfo("fg", "editor.exe", "Editor", work)
    pip = WindowInfo("pip", "chrome.exe", "pip", Rect(80, 40, 160, 120))
    for now, cursor, buttons, active, age in [
        (0.0, (10.0, 10.0), False, False, 0.0),
        (0.5, (20.0, 20.0), False, True, 0.1),
        (1.0, (20.0, 20.0), True, True, 0.05),
        (1.5, (90.0, 80.0), False, True, 0.05),
    ]:
        current = Observation(
            cursor=cursor,
            buttons_down=buttons,
            pip=pip,
            foreground=foreground,
            work_area=work,
            input_active=active,
            input_age=age,
        )
        tracker_observations.append(
            {"now": now, "observation": observation_spec(current)}
        )
        tracker.observe(now, current)
    tracker_risks_input = [
        {"target": [0, 0, 48, 40], "reference": rect(work)},
        {"target": [80, 40, 160, 120], "reference": rect(work)},
    ]
    activity_cases = [
        {
            "name": "normalized_decay_whole_rect",
            "map": {
                "columns": 10,
                "rows": 7,
                "half_life": 2.0,
                "operations": map_operations,
            },
            "densities": density_inputs,
            "tracker_observations": tracker_observations,
            "tracker_risks": tracker_risks_input,
        }
    ]
    activity_expected = [
        {
            "name": "normalized_decay_whole_rect",
            "sparse": map.as_sparse(),
            "densities": [
                map.density_for_rect(
                    Rect(*item["target"]), Rect(*item["reference"])
                )
                for item in density_inputs
            ],
            "tracker_risks": [
                {
                    "activity": tracker.risk_for(
                        Rect(*item["target"]), Rect(*item["reference"])
                    ),
                    "manual": tracker.manual_preference_for(
                        Rect(*item["target"]), Rect(*item["reference"])
                    ),
                }
                for item in tracker_risks_input
            ],
            "tracker_had_activity": tracker.had_activity,
        }
    ]

    visual_bounds = Rect(0, 0, 800, 450)
    visual_pip = Rect(100, 80, 300, 220)
    visual_work = visual_bounds
    visual_observations = []
    visual_expected_observations = []
    visual_tracker = VisualActivityTracker(columns=8, rows=5)
    risk_targets = [[280, 180, 380, 320], [100, 80, 300, 220]]
    for now, level, target_pip in [
        (0.0, 60, visual_pip),
        (0.5, 210, visual_pip),
        (1.0, 140, visual_pip),
        (1.5, 225, Rect(300, 200, 500, 400)),
        (2.0, 120, Rect(300, 200, 500, 400)),
    ]:
        frame_data = frame_spec(
            visual_bounds,
            80,
            45,
            60,
            [(Rect(30, 22, 37, 30), level)],
            (target_pip,),
        )
        pixels = bytearray([60] * (80 * 45))
        for y in range(22, 30):
            for x in range(30, 37):
                pixels[y * 80 + x] = level
        frame = VisualFrame(
            80,
            45,
            bytes(pixels),
            visual_bounds,
            (target_pip,),
        )
        evidence = visual_tracker.observe(
            now, frame, pip_rect=target_pip, context_key="visual-case"
        )
        visual_observations.append(
            {
                "now": now,
                "frame": frame_data,
                "pip": rect(target_pip),
                "context": "visual-case",
                "risk_targets": risk_targets,
            }
        )
        visual_expected_observations.append(
            {
                "valid": evidence.valid,
                "burst": evidence.burst,
                "sustained": evidence.sustained,
                "conflict": evidence.conflict,
                "change_ratio": evidence.change_ratio,
                "available": visual_tracker.availability_at(now),
                "grace": visual_tracker.grace_active_at(now),
                "risks": [
                    visual_tracker.risk_for(
                        Rect(*target), None, now=now
                    )
                    for target in risk_targets
                ],
            }
        )
    visual_cases = [
        {
            "name": "sustained_masks_and_geometry",
            "columns": 8,
            "rows": 5,
            "observations": visual_observations,
        }
    ]
    visual_expected = [
        {
            "name": "sustained_masks_and_geometry",
            "observations": visual_expected_observations,
        }
    ]

    fixture_work = Rect(0, 0, 1920, 1080)
    fixture_foreground = WindowInfo(
        "fg", "editor.exe", "Editor", fixture_work
    )
    fixture_pip = Rect(100, 100, 420, 280)

    def core_observation(
        *,
        cursor: tuple[int, int] = (510, 250),
        pip_rect: Rect = fixture_pip,
        buttons: bool = False,
        foreground: WindowInfo = fixture_foreground,
        pip_id: str = "pip-handle",
        visual: VisualFrame | None = None,
        caret: Rect | None = None,
        input_active: bool = True,
        input_age: float = 0.1,
        exclusions: tuple[Rect, ...] = (),
        work: Rect = fixture_work,
    ) -> Observation:
        return Observation(
            cursor=cursor,
            buttons_down=buttons,
            pip=WindowInfo(
                pip_id, "chrome.exe", "Chrome_WidgetWin_1", pip_rect
            ),
            foreground=foreground,
            work_area=work,
            visual_frame=visual,
            caret=caret,
            input_active=input_active,
            input_age=input_age,
            interaction_exclusions=exclusions,
        )

    class Scenario:
        def __init__(
            self, temporary: Path, name: str, observe_only: bool = False
        ) -> None:
            self.events: list[dict[str, Any]] = []
            self.decisions: list[dict[str, Any]] = []
            self.store = LearningStore(temporary / f"{name}.sqlite3")
            self.controller = AdaptiveController(
                self.store, observe_only=observe_only
            )

        def tick(self, now: float, current: Observation) -> Any:
            self.events.append(
                {"now": now, "observation": observation_spec(current)}
            )
            decision = self.controller.tick(now, current)
            self.decisions.append(decision_spec(decision))
            return decision

        def finish(self) -> tuple[dict[str, Any], dict[str, Any]]:
            status = self.controller.status()
            insights = self.controller.insights()
            self.controller.close()
            self.store.close()
            return (
                {
                    "name": self.controller.store.path
                    if hasattr(self.controller, "store")
                    else "",
                    "events": self.events,
                    "observe_only": self.controller.observe_only,
                },
                {
                    "name": "",
                    "decisions": self.decisions,
                    "status": status,
                    "insights": insights,
                },
            )

    controller_inputs: list[dict[str, Any]] = []
    controller_expected: list[dict[str, Any]] = []
    with tempfile.TemporaryDirectory(
        prefix="schiva-python-fixtures-"
    ) as temporary_name:
        temporary = Path(temporary_name)

        smooth = Scenario(temporary, "smooth")
        smooth.tick(0.0, core_observation())
        decision = smooth.tick(0.04, core_observation())
        current_rect = fixture_pip
        now = 0.04
        target = decision.recommendation
        while smooth.controller.status()["motion_active"] and now < 0.8:
            if decision.move_to is not None:
                current_rect = current_rect.moved_to(*decision.move_to)
            now += 1.0 / 120.0
            decision = smooth.tick(
                now, core_observation(pip_rect=current_rect)
            )
        if target is None:
            raise RuntimeError("smooth fixture did not select a target")
        while (
            smooth.controller.status()["learning_count"] == 0 and now < 14.0
        ):
            now += 0.25
            decision = smooth.tick(
                now,
                core_observation(
                    cursor=(1100, 700),
                    pip_rect=current_rect,
                    input_active=True,
                    input_age=0.1,
                ),
            )
        smooth_input, smooth_expected = smooth.finish()
        smooth_input["name"] = "smooth_motion_quiet_reward"
        smooth_expected["name"] = "smooth_motion_quiet_reward"
        controller_inputs.append(smooth_input)
        controller_expected.append(smooth_expected)

        manual = Scenario(temporary, "manual")
        manual.tick(0.0, core_observation())
        decision = manual.tick(0.04, core_observation())
        if decision.move_to is None:
            raise RuntimeError("manual fixture did not start moving")
        applied = fixture_pip.moved_to(*decision.move_to)
        corrected = Rect(140, 100, 460, 280)
        manual.tick(
            0.18, core_observation(pip_rect=applied, buttons=True)
        )
        manual.tick(
            0.19, core_observation(pip_rect=corrected, buttons=True)
        )
        manual.tick(0.20, core_observation(pip_rect=corrected))
        manual.tick(
            0.25,
            core_observation(
                cursor=(1100, 700), pip_rect=corrected
            ),
        )
        manual_input, manual_expected = manual.finish()
        manual_input["name"] = "manual_hold_correction"
        manual_expected["name"] = "manual_hold_correction"
        controller_inputs.append(manual_input)
        controller_expected.append(manual_expected)

        visual = Scenario(temporary, "visual-context")
        local_work = Rect(0, 0, 800, 450)
        local_pip = Rect(100, 80, 300, 220)
        local_foreground = WindowInfo(
            "local-fg", "editor.exe", "Editor", local_work
        )

        def local_frame(level: int) -> VisualFrame:
            pixels = bytearray([60] * (80 * 45))
            for y in range(22, 30):
                for x in range(30, 37):
                    pixels[y * 80 + x] = level
            return VisualFrame(
                80, 45, bytes(pixels), local_work, (local_pip,)
            )

        visual.tick(
            0.0,
            core_observation(
                cursor=(700, 400),
                pip_rect=local_pip,
                foreground=local_foreground,
                visual=local_frame(60),
                work=local_work,
            ),
        )
        visual.tick(
            0.5,
            core_observation(
                cursor=(700, 400),
                pip_rect=local_pip,
                foreground=local_foreground,
                visual=local_frame(210),
                work=local_work,
            ),
        )
        visual.tick(
            1.0,
            core_observation(
                cursor=(700, 400),
                pip_rect=local_pip,
                foreground=local_foreground,
                visual=local_frame(140),
                work=local_work,
            ),
        )
        visual.tick(
            1.05,
            core_observation(
                cursor=(700, 400),
                pip_rect=local_pip,
                foreground=local_foreground,
                exclusions=(Rect(650, 350, 760, 440),),
                work=local_work,
            ),
        )
        snapped = WindowInfo(
            "snapped-fg", "editor.exe", "Editor", Rect(960, 0, 1920, 1080)
        )
        visual.tick(
            1.2,
            core_observation(
                cursor=(1100, 700),
                pip_rect=Rect(600, 200, 920, 380),
                foreground=snapped,
            ),
        )
        visual_input, visual_expected_result = visual.finish()
        visual_input["name"] = "visual_helper_context"
        visual_expected_result["name"] = "visual_helper_context"
        controller_inputs.append(visual_input)
        controller_expected.append(visual_expected_result)

        observed = Scenario(temporary, "observe-only", observe_only=True)
        observed.tick(0.0, core_observation())
        observed.tick(0.13, core_observation())
        observed.tick(
            0.25,
            core_observation(cursor=(1100, 700), input_active=False),
        )
        observe_input, observe_expected = observed.finish()
        observe_input["name"] = "observe_only"
        observe_expected["name"] = "observe_only"
        controller_inputs.append(observe_input)
        controller_expected.append(observe_expected)

    output = {
        "metadata": {
            "source": str(args.reference.resolve()),
            "source_head": reference_head(args.reference),
            "version": "0.3.0",
            "generated_by": "tools/export_reference.py",
        },
        "input": {
            "linucb": linucb_cases,
            "geometry": geometry_cases,
            "activity": activity_cases,
            "visual": visual_cases,
            "controllers": controller_inputs,
        },
        "expected": {
            "linucb": linucb_expected,
            "geometry": geometry_expected,
            "activity": activity_expected,
            "visual": visual_expected,
            "controllers": controller_expected,
        },
    }
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(
        json.dumps(output, indent=2, sort_keys=True, allow_nan=False) + "\n",
        encoding="utf-8",
    )
    print(
        f"wrote {args.output} from {args.reference} "
        f"({len(controller_inputs)} integrated controllers)"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
