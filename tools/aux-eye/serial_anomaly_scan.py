#!/usr/bin/env python3
from __future__ import annotations

import argparse
import json
import re
import sys
from pathlib import Path

from _expr_eval import ExpressionSyntaxError, Namespace, evaluate, parse


_LOG_LINE = re.compile(r"^\[(OK|DBG|WARN|ERR|FATAL)\]\[(\w+)\]\s+(.+)$")
_KEY_VALUE = re.compile(r"(?<!\S)([A-Za-z_][A-Za-z0-9_]*)=([^\s]+)")
_ANOMALY_LEVELS = frozenset(("ERR", "FATAL"))


def _read_capture(input_name: str) -> bytes:
    if input_name == "-":
        return sys.stdin.buffer.read()
    return Path(input_name).read_bytes()


def _scan(capture: bytes) -> tuple[list[dict], dict[str, str], int]:
    matches = []
    serial_values = {}
    structured_lines = 0
    for raw_line in capture.splitlines():
        line = raw_line.decode("utf-8", errors="replace")
        parsed = _LOG_LINE.fullmatch(line)
        if parsed is None:
            continue
        structured_lines += 1
        level, module, body = parsed.groups()
        for key_value in _KEY_VALUE.finditer(body):
            serial_values[key_value.group(1)] = key_value.group(2)
        if level in _ANOMALY_LEVELS:
            matches.append({"line": line, "level": level, "module": module})
    return matches, serial_values, structured_lines


def _append_evidence(path: str, audit: dict) -> None:
    evidence_path = Path(path)
    evidence_path.parent.mkdir(parents=True, exist_ok=True)
    with evidence_path.open("a", encoding="utf-8") as evidence_file:
        evidence_file.write(json.dumps(audit, ensure_ascii=True, sort_keys=True))
        evidence_file.write("\n")


def _parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(
        description="Scan captured serial bytes for structured anomalies."
    )
    parser.add_argument(
        "--input",
        required=True,
        help="Raw serial capture path, or '-' to read bytes from stdin.",
    )
    parser.add_argument(
        "--predicate",
        help="Optional restricted expression over parsed serial key=value fields.",
    )
    parser.add_argument(
        "--evidence",
        help="Optional path receiving append-only JSON audit records.",
    )
    return parser


def main() -> int:
    arguments = _parser().parse_args()
    try:
        predicate = parse(arguments.predicate) if arguments.predicate is not None else None
    except ExpressionSyntaxError as error:
        if arguments.evidence is not None:
            audit = {
                "input": arguments.input,
                "matches": [],
                "predicate": arguments.predicate,
                "predicate_result": None,
                "structured_lines": 0,
                "verdict": "usage-error",
                "error": str(error),
            }
            try:
                _append_evidence(arguments.evidence, audit)
            except OSError as evidence_error:
                print(f"[ERR][anomaly] cannot append evidence: {evidence_error}", file=sys.stderr)
                return 2
        print(f"[ERR][anomaly] predicate syntax error: {error}", file=sys.stderr)
        return 2

    try:
        capture = _read_capture(arguments.input)
    except OSError as error:
        print(f"[ERR][anomaly] cannot read input: {error}", file=sys.stderr)
        return 2

    matches, serial_values, structured_lines = _scan(capture)
    predicate_result = (
        evaluate(predicate, Namespace(serial=serial_values, events={}))
        if predicate is not None
        else None
    )
    anomaly = bool(matches) or predicate_result is True
    audit = {
        "input": arguments.input,
        "matches": matches,
        "predicate": arguments.predicate,
        "predicate_result": predicate_result,
        "structured_lines": structured_lines,
        "verdict": "ANOMALY" if anomaly else "PASS",
    }
    if arguments.evidence is not None:
        try:
            _append_evidence(arguments.evidence, audit)
        except OSError as error:
            print(f"[ERR][anomaly] cannot append evidence: {error}", file=sys.stderr)
            return 2


    for match in matches:
        print(json.dumps(match, ensure_ascii=True, sort_keys=False))
    if anomaly:
        print(
            "[ERR][anomaly] detected "
            f"matches={len(matches)} predicate={str(predicate_result).lower()}",
            file=sys.stderr,
        )
        return 3
    print(
        "[OK][anomaly] no anomaly "
        f"structured_lines={structured_lines} predicate={str(predicate_result).lower()}",
        file=sys.stderr,
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
