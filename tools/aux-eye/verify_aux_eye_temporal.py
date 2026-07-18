#!/usr/bin/env python3
from __future__ import annotations

import argparse
import hashlib
import json
import math
import sys
from datetime import datetime
from pathlib import Path
from typing import Any

import jsonschema


def _default_schema() -> Path:
    return Path(__file__).resolve().parents[2] / "schemas" / "aux-eye" / "temporal.schema.json"


def _load_json(source: str) -> Any:
    raw = sys.stdin.read() if source == "-" else Path(source).read_text(encoding="utf-8")
    return json.loads(raw)


def _write_evidence(path: str | None, audit: dict[str, Any]) -> None:
    if path is None:
        return
    evidence = Path(path)
    evidence.parent.mkdir(parents=True, exist_ok=True)
    with evidence.open("a", encoding="utf-8") as stream:
        stream.write(json.dumps(audit, sort_keys=True, ensure_ascii=False) + "\n")


def _persist_evidence(path: str | None, audit: dict[str, Any]) -> bool:
    try:
        _write_evidence(path, audit)
    except OSError as exc:
        print(f"[ERR][verify] evidence error: {exc}", file=sys.stderr)
        return False
    return True


def _contained_frame(sequence_dir: Path, raw_path: str) -> Path:
    relative = Path(raw_path)
    if relative.is_absolute() or ".." in relative.parts:
        raise ValueError("frame path must be contained and relative")
    frame = (sequence_dir / relative).resolve(strict=True)
    frame.relative_to(sequence_dir)
    if not frame.is_file():
        raise ValueError("frame path is not a file")
    return frame


def _timestamp(value: int | float | str) -> float:
    if isinstance(value, bool):
        raise ValueError("boolean timestamp")
    if isinstance(value, (int, float)):
        result = float(value)
        if not math.isfinite(result):
            raise ValueError("non-finite timestamp")
        return result
    if not isinstance(value, str):
        raise ValueError("timestamp must be a string or number")
    normalized = value[:-1] + "+00:00" if value.endswith("Z") else value
    return datetime.fromisoformat(normalized).timestamp()


def _window_failures(
    observation: dict[str, Any], sequence_dir: Path
) -> tuple[float, float, list[str]]:
    frames = observation["frames"]
    events = observation.get("temporal_events", [])
    resolved_frames = [_contained_frame(sequence_dir, frame["path"]) for frame in frames]
    failures: list[str] = []
    for index, (frame, path) in enumerate(zip(frames, resolved_frames)):
        actual = hashlib.sha256(path.read_bytes()).hexdigest()
        if frame["sha256"].lower() != actual:
            failures.append(f"frame-identity sha256 mismatch at frames[{index}]")
        embedded = frame.get("observation")
        if isinstance(embedded, dict) and embedded["source_frame_sha256"].lower() != actual:
            failures.append(f"frame-identity sha256 mismatch at frames[{index}].observation")

    try:
        timestamps = [_timestamp(frame["ts"]) for frame in frames]
    except (TypeError, ValueError) as exc:
        failures.append(f"timestamp order: invalid timestamp ({exc})")
        return math.nan, math.nan, failures
    if any(current <= previous for previous, current in zip(timestamps, timestamps[1:])):
        failures.append("timestamp order: timestamps must be strictly increasing")
    rolled_up = any(
        isinstance(frame.get("observation"), dict)
        and frame["observation"].get("visible") is True
        for frame in frames
    )
    if observation["visible"] is not rolled_up:
        failures.append("visible roll-up does not match per-frame observations")

    for index, event in enumerate(events):
        start = event["start_frame"]
        end = event["end_frame"]
        if not 0 <= start <= end < len(frames):
            failures.append(f"frame-index: temporal_events[{index}] is outside frames")
    return timestamps[0], timestamps[-1], failures


def _history_windows(
    history: Path, validator: jsonschema.Draft7Validator
) -> list[tuple[float, float, dict[str, Any]]]:
    windows: list[tuple[float, float, dict[str, Any]]] = []
    for entry in history.iterdir():
        if not entry.is_dir():
            raise ValueError(f"history entry {entry.name} is not a cycle directory")
        try:
            cycle_dir = entry.resolve(strict=True)
            cycle_dir.relative_to(history)
            value = _load_json(str(cycle_dir / "observation.json"))
            if not isinstance(value, dict):
                raise ValueError("observation must be a JSON object")
            validator.validate(value)
            start, end, failures = _window_failures(value, cycle_dir)
            if failures:
                raise ValueError("; ".join(failures))
        except jsonschema.ValidationError as exc:
            raise ValueError(
                f"history window {entry.name} is schema-invalid: {exc.message}"
            ) from exc
        except (OSError, ValueError, json.JSONDecodeError) as exc:
            raise ValueError(f"history window {entry.name} is invalid: {exc}") from exc
        windows.append((start, end, value))
    windows.sort(key=lambda window: (window[0], window[1]))
    for previous, current in zip(windows, windows[1:]):
        if current[0] <= previous[1]:
            raise ValueError("history chronology has duplicate or overlapping windows")
    return windows


def _history_streaks(windows: list[tuple[float, float, dict[str, Any]]]) -> dict[str, int]:
    streaks: dict[str, int] = {}
    for _, _, document in windows:
        events = document.get("temporal_events", [])
        detected_kinds = {
            event["kind"] for event in events if event["status"] == "detected"
        }
        previous = streaks.copy()
        for event in events:
            if event["status"] != "detected":
                if event["confirmations"] != 0:
                    raise ValueError(
                        f"history confirmations for {event['kind']} must be zero when not detected"
                    )
                continue
            allowed = 1 + previous.get(event["kind"], 0)
            if event["confirmations"] != allowed:
                raise ValueError(
                    f"history confirmations for {event['kind']} must equal {allowed}"
                )
        streaks = {
            kind: previous.get(kind, 0) + 1 if kind in detected_kinds else 0
            for kind in previous.keys() | detected_kinds
        }
    return streaks


def _parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description="Aux-eye temporal observation verifier.")
    parser.add_argument("--sequence-dir", required=True)
    parser.add_argument("--observation", required=True)
    parser.add_argument(
        "--schema",
        default=str(_default_schema()),
        help="Path to the temporal observation JSON Schema (default: schemas/aux-eye/temporal.schema.json).",
    )
    parser.add_argument("--evidence")
    parser.add_argument("--history")
    return parser


def main() -> int:
    args = _parser().parse_args()
    audit: dict[str, Any] = {
        "sequence_dir": args.sequence_dir,
        "observation": args.observation,
        "schema": args.schema,
        "history": args.history,
    }

    try:
        sequence_dir = Path(args.sequence_dir).resolve(strict=True)
        if not sequence_dir.is_dir():
            raise ValueError("sequence directory is not a directory")
        schema = _load_json(args.schema)
        observation = _load_json(args.observation)
        if not isinstance(schema, dict) or not isinstance(observation, dict):
            raise ValueError("schema and observation must be JSON objects")
        jsonschema.Draft7Validator.check_schema(schema)
        validator = jsonschema.Draft7Validator(schema)
        history = None if args.history is None else Path(args.history).resolve(strict=True)
        if history is not None and not history.is_dir():
            raise ValueError("history is not a directory")
    except (OSError, ValueError, json.JSONDecodeError, jsonschema.SchemaError) as exc:
        print(f"[ERR][verify] input error: {exc}", file=sys.stderr)
        return 2

    try:
        validator.validate(observation)
        audit["schema_shape"] = "PASS"
    except jsonschema.ValidationError as exc:
        audit["schema_shape"] = "FAIL"
        failure = f"schema-shape: {exc.message}"
        path = tuple(exc.absolute_path)
        if len(path) >= 3 and path[-1] in {"start_frame", "end_frame"}:
            failure = f"{failure} (frame-index)"
        if not _persist_evidence(
            args.evidence, audit | {"verdict": "FAIL", "failures": [failure]}
        ):
            return 2
        print("FAIL")
        print(f"[ERR][verify] temporal observation FAILED: {failure}", file=sys.stderr)
        return 1

    try:
        current_start, _, failures = _window_failures(observation, sequence_dir)
    except (OSError, ValueError) as exc:
        print(f"[ERR][verify] frame path error: {exc}", file=sys.stderr)
        return 2
    try:
        if math.isfinite(current_start):
            windows = [] if history is None else _history_windows(history, validator)
            if windows and current_start <= windows[-1][1]:
                raise ValueError("history chronology does not precede the current window")
            streaks = _history_streaks(windows)
        else:
            streaks = {}
    except (OSError, ValueError, json.JSONDecodeError) as exc:
        print(f"[ERR][verify] input error: {exc}", file=sys.stderr)
        return 2

    for index, event in enumerate(observation.get("temporal_events", [])):
        allowed = 1 + streaks.get(event["kind"], 0)
        if event["confirmations"] > allowed:
            failures.append(
                f"confirmations: temporal_events[{index}] claims {event['confirmations']}, maximum is {allowed}"
            )

    if failures:
        audit.update(verdict="FAIL", failures=failures)
        if not _persist_evidence(args.evidence, audit):
            return 2
        print("FAIL")
        print(
            f"[ERR][verify] temporal observation FAILED {len(failures)} check(s)",
            file=sys.stderr,
        )
        for failure in failures:
            print(f"  - {failure}", file=sys.stderr)
        return 1

    audit["verdict"] = "PASS"
    if not _persist_evidence(args.evidence, audit):
        return 2
    print("PASS")
    print(
        "[OK][verify] temporal observation PASSED all deterministic gates",
        file=sys.stderr,
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
