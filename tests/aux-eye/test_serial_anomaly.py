from __future__ import annotations

import ast
import json
import re
import subprocess
import sys
from pathlib import Path

import pytest


_REPO_ROOT = Path(__file__).resolve().parents[2]
_SCANNER = _REPO_ROOT / "tools" / "aux-eye" / "serial_anomaly_scan.py"
_EVALUATOR = _REPO_ROOT / "tools" / "aux-eye" / "_expr_eval.py"


def _run_scanner(
    serial_text: str | None = None,
    arguments: list[str] | None = None,
) -> subprocess.CompletedProcess:
    command = [sys.executable, str(_SCANNER), *(arguments or ("--input", "-"))]
    return subprocess.run(
        command,
        input=serial_text,
        capture_output=True,
        text=True,
        cwd=str(_REPO_ROOT),
    )


def _output_rows(result: subprocess.CompletedProcess) -> list[dict]:
    return [json.loads(line) for line in result.stdout.splitlines()]


def test_help_exits_zero():
    # Given: the scanner command.
    # When: its help is requested.
    result = subprocess.run(
        [sys.executable, str(_SCANNER), "--help"],
        capture_output=True,
        text=True,
        cwd=str(_REPO_ROOT),
    )

    # Then: argparse reports the supported input contract successfully.
    assert result.returncode == 0
    assert "--input" in result.stdout


def test_stdin_emits_only_err_and_fatal_lines():
    # Given: structured serial input with all severity classes.
    serial_text = (
        "[OK][control] state=ready\n"
        "[DBG][control] sample=1\n"
        "[WARN][control] lag=2\n"
        "[ERR][startup] code=7\n"
        "[FATAL][startup] code=9\n"
    )

    # When: the default anomaly scan reads stdin.
    result = _run_scanner(serial_text)

    # Then: only ERR/FATAL lines are emitted and the anomaly exit code is used.
    assert result.returncode == 3
    assert _output_rows(result) == [
        {"line": "[ERR][startup] code=7", "level": "ERR", "module": "startup"},
        {
            "line": "[FATAL][startup] code=9",
            "level": "FATAL",
            "module": "startup",
        },
    ]
    assert "[ERR][anomaly]" in result.stderr


def test_file_input_with_ok_and_dbg_has_no_anomaly(tmp_path):
    # Given: an on-disk capture containing only normal structured levels.
    capture = tmp_path / "serial.log"
    capture.write_text(
        "[OK][control] state=ready\n[DBG][control] sample=1\n",
        encoding="utf-8",
    )

    # When: the scanner reads the capture path.
    result = _run_scanner(arguments=["--input", str(capture)])

    # Then: no rows are emitted and the success exit code is used.
    assert result.returncode == 0
    assert result.stdout == ""
    assert "[OK][anomaly]" in result.stderr


def test_boot_and_unstructured_lines_are_not_structured_anomalies():
    # Given: reset banners and free text outside the level/module contract.
    serial_text = "[BOOT] reset=1\nplain text\n[ERR] incomplete\n"

    # When: the default scanner processes them.
    result = _run_scanner(serial_text)

    # Then: none match the structured ERR/FATAL grammar.
    assert result.returncode == 0
    assert result.stdout == ""


def test_true_predicate_is_an_anomaly_and_collects_key_values():
    # Given: key=value telemetry split across structured lines.
    serial_text = "[OK][control] uptime=5 mode=run\n[DBG][control] sample=2\n"

    # When: a predicate over the parsed serial namespace is true.
    result = _run_scanner(
        serial_text, ["--input", "-", "--predicate", "serial.uptime > 0"]
    )

    # Then: predicate truth produces the anomaly exit code without a severity row.
    assert result.returncode == 3
    assert result.stdout == ""
    assert "predicate=true" in result.stderr


def test_false_and_missing_predicates_are_not_anomalies():
    # Given: structured telemetry without the requested or satisfying values.
    serial_text = "[OK][control] uptime=0 mode=idle\n"

    # When: false and missing predicates are scanned independently.
    false_result = _run_scanner(
        serial_text, ["--input", "-", "--predicate", "serial.uptime > 0"]
    )
    missing_result = _run_scanner(
        serial_text,
        ["--input", "-", "--predicate", "serial.missing_key > 5"],
    )

    # Then: both remain normal scans.
    assert false_result.returncode == 0
    assert missing_result.returncode == 0
    assert "predicate=false" in false_result.stderr
    assert "predicate=false" in missing_result.stderr


def test_malformed_predicate_returns_usage_error():
    # Given: real stdin plus a syntactically incomplete predicate.
    # When: the scanner parses the predicate before evaluating it.
    result = _run_scanner(
        "[OK][control] uptime=1\n",
        ["--input", "-", "--predicate", "serial.uptime >"],
    )

    # Then: syntax maps to the reserved usage error code.
    assert result.returncode == 2
    assert result.stdout == ""
    assert "syntax" in result.stderr.lower()


def test_missing_input_file_returns_usage_error(tmp_path):
    # Given: a path that is not a readable capture.
    missing = tmp_path / "missing.log"

    # When: the scanner opens it.
    result = _run_scanner(arguments=["--input", str(missing)])

    # Then: boundary I/O failure maps to the usage error code.
    assert result.returncode == 2
    assert "cannot read" in result.stderr.lower()


def test_evidence_is_append_only_for_normal_and_anomalous_scans(tmp_path):
    # Given: one shared evidence path.
    evidence = tmp_path / "audit.jsonl"

    # When: a normal scan and an anomaly scan append audits.
    normal = _run_scanner(
        "[OK][control] value=0\n",
        ["--input", "-", "--evidence", str(evidence)],
    )
    anomalous = _run_scanner(
        "[ERR][control] value=1\n",
        [
            "--input",
            "-",
            "--predicate",
            "serial.value == 1",
            "--evidence",
            str(evidence),
        ],
    )

    # Then: both complete and remain as separate valid JSON records.
    assert normal.returncode == 0
    assert anomalous.returncode == 3
    audits = [json.loads(line) for line in evidence.read_text(encoding="utf-8").splitlines()]
    assert [audit["verdict"] for audit in audits] == ["PASS", "ANOMALY"]
    assert audits[1]["predicate_result"] is True
    assert audits[1]["matches"] == [
        {"line": "[ERR][control] value=1", "level": "ERR", "module": "control"}
    ]


def test_later_key_value_replaces_earlier_value():
    # Given: append-only telemetry updating the same key in a later line.
    serial_text = "[OK][control] value=0\n[DBG][control] value=2\n"

    # When: the predicate evaluates the completed capture namespace.
    result = _run_scanner(
        serial_text, ["--input", "-", "--predicate", "serial.value == 2"]
    )

    # Then: the latest observed value is used.
    assert result.returncode == 3


def test_tool_sources_keep_restricted_io_and_evaluation_boundaries():
    # Given: both Todo 3 source modules parsed as Python syntax trees.
    trees = {
        path: ast.parse(path.read_text(encoding="utf-8"))
        for path in (_SCANNER, _EVALUATOR)
    }
    forbidden_imports = {
        "anthropic",
        "httpx",
        "openai",
        "requests",
        "serial",
        "socket",
        "urllib",
    }

    # When: imports and direct calls are inspected structurally.
    imported_roots = {
        imported_name.split(".", maxsplit=1)[0]
        for tree in trees.values()
        for node in ast.walk(tree)
        if isinstance(node, (ast.Import, ast.ImportFrom))
        for imported_name in (
            [alias.name for alias in node.names]
            if isinstance(node, ast.Import)
            else [node.module or ""]
        )
    }
    direct_calls = {
        node.func.id
        for tree in trees.values()
        for node in ast.walk(tree)
        if isinstance(node, ast.Call) and isinstance(node.func, ast.Name)
    }

    # Then: no network/serial dependency or dynamic evaluator exists.
    assert imported_roots.isdisjoint(forbidden_imports)
    assert direct_calls.isdisjoint({"eval", "exec"})
    assert not any(
        isinstance(node, (ast.Import, ast.ImportFrom))
        for node in ast.walk(trees[_EVALUATOR])
    )
    assert "serial.Serial" not in _SCANNER.read_text(encoding="utf-8")


def test_tool_sources_have_no_hardcoded_morphology():
    # Given: the complete Todo 3 implementation text.
    source = "\n".join(
        path.read_text(encoding="utf-8") for path in (_SCANNER, _EVALUATOR)
    )

    # When / Then: robot-specific morphology terms are absent.
    assert re.search(r"tilt|平衡车|wheel|rpm|angle", source, re.IGNORECASE) is None


@pytest.mark.parametrize(
    "predicate",
    [
        "serial.x == 1 eof",
        "(" * 1500 + "serial.x == 1",
        "(" * 1500 + "serial.x == 1" + ")" * 1500,
        "!" * 1500 + "serial.x == 1",
        " && ".join("serial.x == 1" for _ in range(1501)),
        " || ".join("serial.x == 1" for _ in range(1501)),
    ],
)
def test_adversarial_predicates_exit_two_and_append_usage_error(
    tmp_path, predicate
):
    # Given: a malformed or over-complex predicate and an evidence destination.
    evidence = tmp_path / "usage-errors.jsonl"

    # When: the real scanner parses it before evaluating telemetry.
    result = _run_scanner(
        "[OK][control] x=1\n",
        [
            "--input",
            "-",
            "--predicate",
            predicate,
            "--evidence",
            str(evidence),
        ],
    )

    # Then: syntax is controlled, traceback-free, and auditable.
    assert result.returncode == 2
    assert result.stdout == ""
    assert "traceback" not in result.stderr.lower()
    audit = json.loads(evidence.read_text(encoding="utf-8"))
    assert audit["verdict"] == "usage-error"
