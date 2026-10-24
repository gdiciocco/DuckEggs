#!/usr/bin/env python3
"""Compare a schema-1 DuckEggs backup with the immutable Python reference."""

from __future__ import annotations

import argparse
import hashlib
import importlib.util
import json
import math
import sqlite3
import subprocess
import sys
import tempfile
from pathlib import Path

TOLERANCE = 1e-12
VECTORS = [
    ("northwest", [1, -1, -1, -1, -1, -1, -1, 0, 0, .15, .05, 0]),
    ("center", [1, 0, 0, 0, 0, 0, 0, 0, 0, 0, .20, 0]),
    ("southeast-low-risk", [1, 1, 1, 1, 1, 1, 1, 0, 0, .60, 0, .10]),
]
COLUMNS = {
    "profiles": {"profile_key", "app_id", "role", "layout_bin", "short_map", "long_map", "manual_map"},
    "decision_records": {"id", "profile_key", "features_json", "action", "reward"},
    "model_state": {"id", "state_json", "updates"},
}


class VerificationError(RuntimeError):
    pass


def digest(path: Path) -> str:
    value = hashlib.sha256()
    with path.open("rb") as source:
        for block in iter(lambda: source.read(1024 * 1024), b""):
            value.update(block)
    return value.hexdigest()


def readonly(path: Path) -> sqlite3.Connection:
    connection = sqlite3.connect(path.resolve().as_uri() + "?mode=ro", uri=True)
    connection.execute("PRAGMA query_only = ON")
    return connection


def require(condition: bool, message: str) -> None:
    if not condition:
        raise VerificationError(message)


def reference_model(path: Path):
    candidates = [path, path / "linucb.py", path / "src/schiva/core/linucb.py",
                  path / "schiva/core/linucb.py"]
    source = next((item for item in candidates if item.is_file()), None)
    require(source is not None, "Python reference linucb.py was not found")
    sys.dont_write_bytecode = True
    spec = importlib.util.spec_from_file_location("_duckeggs_linucb", source)
    require(spec is not None and spec.loader is not None, "cannot load Python reference")
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module.LinUCBModel


def decode_map(raw: object) -> dict[str, float]:
    try:
        value = json.loads(raw) if isinstance(raw, str) else {}
    except (TypeError, ValueError):
        return {}
    if not isinstance(value, dict):
        return {}
    result = {}
    for key, item in value.items():
        try:
            number = float(item)
        except (TypeError, ValueError):
            continue
        if math.isfinite(number) and 0 <= number <= 1000:
            result[str(key)] = number
    return result


def close(actual: object, expected: object, path: str = "report") -> None:
    if isinstance(expected, dict):
        require(isinstance(actual, dict) and set(actual) == set(expected), f"field mismatch at {path}")
        for key in expected:
            close(actual[key], expected[key], f"{path}.{key}")
    elif isinstance(expected, list):
        require(isinstance(actual, list) and len(actual) == len(expected), f"length mismatch at {path}")
        for index, pair in enumerate(zip(actual, expected)):
            close(pair[0], pair[1], f"{path}[{index}]")
    elif isinstance(expected, (int, float)) and not isinstance(expected, bool):
        require(isinstance(actual, (int, float)) and not isinstance(actual, bool)
                and math.isclose(float(actual), float(expected), rel_tol=TOLERANCE, abs_tol=TOLERANCE),
                f"numeric mismatch at {path}")
    else:
        require(actual == expected, f"value mismatch at {path}")


def scores(model, features: list[float]) -> dict[str, float]:
    return {"prediction": model.predict(features), "score": model.score(features)}


def run(args: argparse.Namespace) -> str:
    backup, inspector = Path(args.backup).resolve(), Path(args.inspector).resolve()
    require(backup.is_file(), "source backup is missing")
    require(inspector.is_file(), "inspector executable is missing")
    Model = reference_model(Path(args.python_reference).resolve())
    before = digest(backup)
    source = None
    try:
        source = readonly(backup)
        version = int(source.execute("PRAGMA user_version").fetchone()[0])
        require(version == 1, f"unsupported or missing schema version: {version}")
        for table, expected in COLUMNS.items():
            present = {str(row[1]) for row in source.execute(f"PRAGMA table_info({table})")}
            require(expected <= present, f"incompatible schema in {table}")
        profiles = [
            {"profile_key": row[0], "app_id": row[1], "role": row[2],
             "layout_bin": row[3], "short_map": decode_map(row[4]),
             "long_map": decode_map(row[5]), "manual_map": decode_map(row[6])}
            for row in source.execute(
                "SELECT profile_key, app_id, role, layout_bin, short_map, long_map, "
                "manual_map FROM profiles ORDER BY profile_key")
        ]
        rows = source.execute(
            "SELECT id, profile_key, features_json, action, reward "
            "FROM decision_records ORDER BY id").fetchall()
        model_rows = source.execute("SELECT id, state_json, updates FROM model_state").fetchall()
        require(len(model_rows) == 1, "model_state must contain exactly one row")
        model_id, state_json, row_updates = model_rows[0]
        try:
            state = json.loads(state_json)
        except (TypeError, ValueError) as error:
            raise VerificationError("persisted model state is invalid JSON") from error
        require(isinstance(state, dict), "persisted model state is not an object")
        model = Model.from_state(state)
        require(int(model.updates) == int(row_updates), "model updates column disagrees with state")
        decisions = []
        for row in rows:
            try:
                features = json.loads(row[2])
            except (TypeError, ValueError) as error:
                raise VerificationError("decision features are invalid JSON") from error
            require(isinstance(features, list), "decision features are not an array")
            decisions.append({
                "id": row[0], "profile_key": row[1], "features": features,
                "action": row[3], "reward": row[4], **scores(model, features)})
        by_profile = {str(key): int(value) for key, value in source.execute(
            "SELECT profile_key, COUNT(*) FROM decision_records GROUP BY profile_key")}

        with tempfile.TemporaryDirectory(prefix="duckeggs-interop-") as directory:
            copy = Path(directory) / "learning.sqlite3"
            with sqlite3.connect(copy) as target:
                source.backup(target)
            source.close()
            source = None
            completed = subprocess.run(
                [str(inspector), str(copy)], capture_output=True, text=True,
                encoding="utf-8", timeout=60, check=False)
            require(completed.returncode == 0,
                    "inspector failed: " + completed.stderr.strip()[:300])
            try:
                actual = json.loads(completed.stdout)
            except ValueError as error:
                raise VerificationError("inspector did not emit valid JSON") from error

        close(actual["schema_version"], version)
        close(actual["model"], {"row_id": model_id, "row_updates": row_updates,
                                "state": model.state()})
        close(actual["profiles"], profiles)
        close(actual["decisions"], decisions)
        close(actual["decision_counts"],
              {"records": len(decisions), "by_profile": by_profile})
        close(actual["action_ranking"],
              sorted(decisions, key=lambda item: (-item["score"], item["id"])))
        vectors = [{"label": label, "features": features, **scores(model, features)}
                   for label, features in VECTORS]
        close(actual["vectors"], vectors)
        close(actual["fixed_vector_ranking"],
              sorted(vectors, key=lambda item: (-item["score"], item["label"])))
        return (f"PASS schema=1 profiles={len(profiles)} decisions={len(decisions)} "
                f"dimension={model.dimension} updates={model.updates} "
                f"tolerance={TOLERANCE:g} sha256={before}")
    finally:
        if source is not None:
            source.close()
        if digest(backup) != before:
            raise VerificationError("source backup changed during verification")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--inspector", required=True)
    parser.add_argument("--backup", required=True)
    parser.add_argument("--python-reference", required=True)
    args = parser.parse_args()
    try:
        print(run(args))
        return 0
    except Exception as error:
        print(f"FAIL: {type(error).__name__}: {error}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
