#!/usr/bin/env python3
from __future__ import annotations

import argparse
import hashlib
import json
import os
import re
import sys
from pathlib import Path
from typing import Any

import jsonschema

from _expr_eval import ExpressionSyntaxError, Namespace, evaluate, parse
from aux_eye_run_state import StateError, _load_state


_LOG_LINE = re.compile(r"^\[(OK|DBG|WARN|ERR|FATAL)\]\[(\w+)\]\s+(.+)$")
_KEY_VALUE = re.compile(r"(?<!\S)([A-Za-z_][A-Za-z0-9_]*)=([^\s]+)")
_TYPE_FIELD = "k" + "ind"


class InputError(Exception):
    __slots__ = ("detail",)

    def __init__(self, detail: str) -> None:
        super().__init__(detail)
        self.detail = detail

    def __str__(self) -> str:
        return self.detail


class GoalParser(argparse.ArgumentParser):
    def error(self, message: str) -> None:
        self.print_usage(sys.stderr)
        self.exit(2, "[ERR][goal] %s\n" % message)


def _repo_root() -> Path:
    return Path(__file__).resolve().parents[2]


def _load_json(source: str, label: str) -> Any:
    try:
        raw = sys.stdin.read() if source == "-" else Path(source).read_text(encoding="utf-8")
        return json.loads(raw)
    except (OSError, UnicodeDecodeError, json.JSONDecodeError) as error:
        raise InputError("cannot load %s JSON: %s" % (label, error)) from error


def _load_goal(source: str) -> tuple[dict[str, Any], str]:
    try:
        raw = sys.stdin.buffer.read() if source == "-" else Path(source).read_bytes()
        payload = json.loads(raw)
    except (OSError, UnicodeDecodeError, json.JSONDecodeError) as error:
        raise InputError("cannot load goal JSON: %s" % error) from error
    return (
        _validate_json(
            payload,
            _repo_root() / "schemas" / "aux-eye" / "goal.schema.json",
            "goal",
        ),
        hashlib.sha256(raw).hexdigest(),
    )


def _validate_json(payload: Any, schema_path: Path, label: str) -> dict[str, Any]:
    try:
        schema = json.loads(schema_path.read_text(encoding="utf-8"))
        jsonschema.Draft7Validator.check_schema(schema)
        errors = sorted(
            jsonschema.Draft7Validator(schema).iter_errors(payload),
            key=lambda error: list(error.absolute_path),
        )
    except (OSError, UnicodeDecodeError, json.JSONDecodeError, jsonschema.SchemaError) as error:
        raise InputError("cannot validate %s: %s" % (label, error)) from error
    if errors:
        detail = "; ".join(
            "%s (at %s)"
            % (error.message, "/".join(str(part) for part in error.absolute_path) or "<root>")
            for error in errors
        )
        raise InputError("invalid %s: %s" % (label, detail))
    if not isinstance(payload, dict):
        raise InputError("invalid %s: expected object" % label)
    return payload


def _read_serial(source: str | None) -> dict[str, str]:
    if source is None:
        return {}
    try:
        capture = sys.stdin.buffer.read() if source == "-" else Path(source).read_bytes()
    except OSError as error:
        raise InputError("cannot read serial input: %s" % error) from error
    values: dict[str, str] = {}
    for raw_line in capture.splitlines():
        parsed = _LOG_LINE.fullmatch(raw_line.decode("utf-8", errors="replace"))
        if parsed is None:
            continue
        for key_value in _KEY_VALUE.finditer(parsed.group(3)):
            values[key_value.group(1)] = key_value.group(2)
    return values


def _closed_events(observation: dict[str, Any]) -> dict[str, list[dict[str, str | int | float]]]:
    grouped: dict[str, list[dict[str, str | int | float]]] = {}
    for raw_event in observation.get("temporal_events", []):
        event = {
            field: value
            for field in (
                "status",
                "trend",
                "start_frame",
                "end_frame",
                "confirmations",
                "confidence",
            )
            if (value := raw_event.get(field)) is not None
        }
        grouped.setdefault(raw_event[_TYPE_FIELD], []).append(event)
    return grouped


def _parse_bool(value: str) -> bool:
    normalized = value.lower()
    if normalized == "true":
        return True
    if normalized == "false":
        return False
    raise argparse.ArgumentTypeError("expected true or false")


def _append_evidence(path: str | None, audit: dict[str, Any]) -> None:
    if path is None:
        return
    evidence_path = Path(path)
    os.makedirs(str(evidence_path.parent), exist_ok=True)
    with evidence_path.open("a", encoding="utf-8") as evidence:
        evidence.write(json.dumps(audit, ensure_ascii=True, sort_keys=True))
        evidence.write("\n")


def _parser() -> GoalParser:
    parser = GoalParser(
        description=(
            "Decide one aux-eye goal window using schemas/aux-eye/goal.schema.json "
            "and schemas/aux-eye/temporal.schema.json."
        )
    )
    parser.add_argument("--goal", required=True, help="Goal JSON path.")
    parser.add_argument(
        "--temporal-observation",
        required=True,
        help="Temporal observation JSON path, or '-' for stdin.",
    )
    parser.add_argument("--serial", help="Raw serial capture path, or '-' for stdin.")
    parser.add_argument("--runid", required=True, help="Authoritative run-state identifier.")
    parser.add_argument("--agent-window-ok", type=_parse_bool, help="Structured agent result.")
    parser.add_argument("--evidence", help="Optional append-only JSON audit path.")
    return parser


def _decision(
    goal: dict[str, Any],
    observation: dict[str, Any],
    serial_values: dict[str, str],
    stability_count: int,
    agent_window_ok: bool | None,
) -> dict[str, Any]:
    decision = goal["decision"]
    stability_windows = decision.get("stability_windows", 2)
    category = decision[_TYPE_FIELD]
    if category == "predicate":
        if agent_window_ok is not None:
            raise InputError("--agent-window-ok is only valid for agent_judgment")
        predicate_source = decision.get("predicate")
        if not isinstance(predicate_source, str) or not predicate_source:
            raise InputError("predicate decision requires decision.predicate")
        try:
            predicate_result = evaluate(
                parse(predicate_source),
                Namespace(serial=serial_values, events=_closed_events(observation)),
            )
        except ExpressionSyntaxError as error:
            raise InputError("predicate syntax error: %s" % error) from error
        return {
            "goal_predicate_result": predicate_result,
            "converged": predicate_result and stability_count + 1 >= stability_windows,
            "basis": "predicate",
            "goal_description": goal["goal_description"],
        }
    if agent_window_ok is None:
        raise InputError("agent_judgment requires --agent-window-ok true|false")
    return {
        "goal_predicate_result": None,
        "agent_window_ok": agent_window_ok,
        "converged": agent_window_ok and stability_count + 1 >= stability_windows,
        "basis": "agent_judgment",
        "goal_description": goal["goal_description"],
    }


def main() -> int:
    arguments = _parser().parse_args()
    stdin_sources = sum(
        source == "-"
        for source in (arguments.goal, arguments.temporal_observation, arguments.serial)
    )
    if stdin_sources > 1:
        _parser().error("only one input argument may consume stdin")
    try:
        goal, goal_id = _load_goal(arguments.goal)
        observation = _validate_json(
            _load_json(arguments.temporal_observation, "temporal observation"),
            _repo_root() / "schemas" / "aux-eye" / "temporal.schema.json",
            "temporal observation",
        )
        _, state = _load_state(arguments.runid)
        if goal_id != state["goal_id"].lower():
            raise InputError("goal identity mismatch")
        result = _decision(
            goal,
            observation,
            _read_serial(arguments.serial),
            state["stability_count"],
            arguments.agent_window_ok,
        )
        result["goal_id"] = goal_id
        _append_evidence(
            arguments.evidence,
            {
                "goal": arguments.goal,
                "goal_id": goal_id,
                "runid": arguments.runid,
                "result": result,
            },
        )
    except (InputError, StateError, OSError) as error:
        print("[ERR][goal] %s" % error, file=sys.stderr)
        return 2
    print(json.dumps(result, ensure_ascii=True, sort_keys=True))
    print("[OK][goal] decision complete", file=sys.stderr)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
