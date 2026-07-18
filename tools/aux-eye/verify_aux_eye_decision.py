#!/usr/bin/env python3
"""Validate one aux-eye iteration decision against its deterministic gates."""

from __future__ import annotations

import argparse
import json
import sys
from pathlib import Path
from typing import Any, Final

import jsonschema


_ROLE_ACTIONS: Final = {
    "veto": frozenset({"safe_abort", "needs_human"}),
    "stop": frozenset({"safe_abort", "needs_human"}),
    "request_more": frozenset({"continue", "needs_human"}),
}


class InputError(Exception):
    __slots__ = ("detail",)

    def __init__(self, detail: str) -> None:
        super().__init__(detail)
        self.detail = detail

    def __str__(self) -> str:
        return self.detail


def _default_schema_path() -> Path:
    return Path(__file__).resolve().parents[2] / "schemas" / "aux-eye" / "decision.schema.json"


def _load_json(argument: str, label: str) -> Any:
    try:
        raw = sys.stdin.read() if argument == "-" else Path(argument).read_text(encoding="utf-8")
        return json.loads(raw)
    except (OSError, UnicodeDecodeError, json.JSONDecodeError) as exc:
        raise InputError(f"cannot load {label} JSON: {exc}") from exc


def _schema_failures(decision: Any, schema: Any) -> list[str]:
    try:
        jsonschema.Draft7Validator.check_schema(schema)
        validator = jsonschema.Draft7Validator(schema)
        return [
            "schema-shape: %s (at %s)"
            % (error.message, "/".join(str(part) for part in error.absolute_path) or "<root>")
            for error in sorted(
                validator.iter_errors(decision),
                key=lambda error: list(error.absolute_path),
            )
        ]
    except (jsonschema.SchemaError, TypeError, AttributeError) as exc:
        raise InputError(f"invalid schema: {exc}") from exc


def _policy_failures(decision: dict[str, Any]) -> list[str]:
    failures: list[str] = []
    changed = decision.get("firmware_parameters_changed")
    basis = decision.get("parameter_change_basis")
    serial_evidence = basis.get("serial_evidence", []) if isinstance(basis, dict) else []

    if changed is True:
        if not serial_evidence:
            failures.append("serial-basis: changed firmware requires nonempty serial_evidence")
        if isinstance(basis, dict) and set(basis) - {"serial_evidence"}:
            failures.append("serial-basis: parameter_change_basis must contain only serial evidence")
    elif changed is False and basis:
        failures.append("inconsistent: unchanged firmware must not declare parameter_change_basis")

    role = decision.get("aux_role")
    action = decision.get("action")
    allowed_actions = _ROLE_ACTIONS.get(role)
    if allowed_actions is not None and action not in allowed_actions:
        failures.append(f"aux-role-conflict: aux_role={role!r} cannot use action={action!r}")
    if action == "success" and decision.get("converged") is not True:
        failures.append("not-converged: success requires converged=true")
    if action == "candidate_success" and decision.get("window_ok") is not True:
        failures.append("not-candidate: candidate_success requires window_ok=true")
    return failures


def _append_audit(path: str | None, audit: dict[str, Any]) -> None:
    if path is None:
        return
    try:
        evidence_path = Path(path)
        evidence_path.parent.mkdir(parents=True, exist_ok=True)
        with evidence_path.open("a", encoding="utf-8") as evidence:
            evidence.write("\n===== verify_aux_eye_decision.py audit =====\n")
            json.dump(audit, evidence, indent=2, ensure_ascii=False)
            evidence.write("\n")
    except OSError as exc:
        raise InputError(f"cannot write evidence: {exc}") from exc


def _parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(
        description="Validate an aux-eye decision using schema and deterministic policy gates."
    )
    parser.add_argument("--decision", required=True, help="Decision JSON path, or '-' for stdin.")
    parser.add_argument(
        "--schema",
        default=str(_default_schema_path()),
        help="Decision JSON Schema path (default: schemas/aux-eye/decision.schema.json).",
    )
    parser.add_argument("--evidence", help="Optional append-only JSON audit path.")
    return parser


def main() -> int:
    args = _parser().parse_args()
    try:
        schema = _load_json(args.schema, "schema")
        decision = _load_json(args.decision, "decision")
        failures = _schema_failures(decision, schema)
    except InputError as exc:
        print(f"[ERR][verify] {exc}", file=sys.stderr)
        return 2

    if isinstance(decision, dict):
        failures.extend(_policy_failures(decision))

    audit: dict[str, Any] = {
        "decision_source": args.decision,
        "schema": args.schema,
        "verdict": "FAIL" if failures else "PASS",
        "failures": failures,
    }
    try:
        _append_audit(args.evidence, audit)
    except InputError as exc:
        print(f"[ERR][verify] {exc}", file=sys.stderr)
        return 2
    if failures:
        print("FAIL")
        print(f"[ERR][verify] decision FAILED {len(failures)} check(s):", file=sys.stderr)
        for failure in failures:
            print(f"  - {failure}", file=sys.stderr)
        return 1

    print("PASS")
    print("[OK][verify] decision PASSED all gates", file=sys.stderr)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
