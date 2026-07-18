import ast
import json
import subprocess
import sys
from pathlib import Path

import pytest


_REPO_ROOT = Path(__file__).resolve().parents[2]
_VERIFY = _REPO_ROOT / "tools" / "aux-eye" / "verify_aux_eye_decision.py"
_SERIAL_REFERENCE = {
    "capture_path": "captures/run-1.log",
    "line_number": 7,
    "line_sha256": "a" * 64,
}


def _decision(**changes):
    decision = {
        "iteration": 1,
        "goal_id": "goal-sha",
        "goal_predicate_result": False,
        "converged": False,
        "window_ok": False,
        "firmware_parameters_changed": True,
        "parameter_change_basis": {"serial_evidence": [_SERIAL_REFERENCE]},
        "serial_basis": {"anomaly_lines": [], "predicate_result": False},
        "aux_events": [],
        "aux_role": "classify",
        "action": "continue",
        "reason": "human-only narrative",
    }
    decision.update(changes)
    return decision


def _run(decision, *args: str) -> subprocess.CompletedProcess[str]:
    return subprocess.run(
        [sys.executable, str(_VERIFY), "--decision", "-", *args],
        input=json.dumps(decision),
        capture_output=True,
        text=True,
        cwd=_REPO_ROOT,
        check=False,
    )


def test_valid_decision_passes_from_stdin():
    # Given / When: a changed-firmware decision cites closed serial evidence.
    result = _run(_decision())

    # Then: every deterministic gate accepts it.
    assert result.returncode == 0, result.stderr
    assert "passed" in result.stderr.lower()
    assert result.stdout.strip() == "PASS"


def test_decision_file_and_schema_options_are_supported(tmp_path: Path):
    # Given: a valid decision stored on disk.
    decision_path = tmp_path / "decision.json"
    decision_path.write_text(json.dumps(_decision()), encoding="utf-8")

    # When: the CLI receives explicit decision and schema paths.
    result = subprocess.run(
        [
            sys.executable,
            str(_VERIFY),
            "--decision",
            str(decision_path),
            "--schema",
            "schemas/aux-eye/decision.schema.json",
        ],
        capture_output=True,
        text=True,
        cwd=_REPO_ROOT,
        check=False,
    )

    # Then: file-based input passes.
    assert result.returncode == 0, result.stderr


@pytest.mark.parametrize(
    ("changes", "message"),
    [
        ({"parameter_change_basis": {"serial_evidence": []}}, "serial-basis"),
        (
            {"parameter_change_basis": {"serial_evidence": [{"capture_path": "x", "line_number": 1}]}},
            "line_sha256",
        ),
        (
            {"parameter_change_basis": {"serial_evidence": [{**_SERIAL_REFERENCE, "line_sha256": "bad"}]}},
            "line_sha256",
        ),
        (
            {"parameter_change_basis": {"serial_evidence": [_SERIAL_REFERENCE], "aux_events": []}},
            "serial-basis",
        ),
        (
            {"parameter_change_basis": {"serial_evidence": [_SERIAL_REFERENCE], "kind": "oscillation"}},
            "serial-basis",
        ),
        (
            {"parameter_change_basis": {"serial_evidence": [_SERIAL_REFERENCE], "suggested_pid_kp": 1.2}},
            "serial-basis",
        ),
        (
            {"firmware_parameters_changed": False},
            "inconsistent",
        ),
        ({"suggested_pid_kp": 1.2}, "additional properties"),
        ({"aux_events": [{"kind": "oscillation", "status": "detected", "suggested_delay_ms": 2}]}, "additional properties"),
        ({"aux_role": "veto", "action": "continue"}, "aux-role-conflict"),
        ({"aux_role": "request_more", "action": "success", "converged": True}, "aux-role-conflict"),
        ({"aux_role": "none", "action": "success"}, "not-converged"),
        ({"aux_role": "none", "action": "candidate_success"}, "not-candidate"),
    ],
)
def test_invalid_decisions_fail_policy(changes, message: str):
    # Given / When: one structural or policy invariant is violated.
    result = _run(_decision(**changes))

    # Then: policy failure is exit 1 with a useful diagnostic.
    assert result.returncode == 1
    assert message in result.stderr.lower()
    assert result.stdout.strip() == "FAIL"


@pytest.mark.parametrize(
    ("role", "action", "allowed"),
    [
        (role, action, role in {"classify", "none"} or action in ({"safe_abort", "needs_human"} if role in {"veto", "stop"} else {"continue", "needs_human"}))
        for role in ("veto", "stop", "request_more", "classify", "none")
        for action in ("continue", "candidate_success", "success", "safe_abort", "needs_human")
    ],
)
def test_complete_aux_role_action_matrix(role: str, action: str, allowed: bool):
    # Given: success-specific fields satisfy their independent gates.
    decision = _decision(
        aux_role=role,
        action=action,
        converged=action == "success",
        window_ok=action == "candidate_success",
    )

    # When: all role/action cells are exercised.
    result = _run(decision)

    # Then: only the documented matrix cells pass.
    assert (result.returncode == 0) is allowed, result.stderr


def test_success_and_candidate_success_positive_paths_pass():
    # Given / When: the structured convergence/window flags authorize each action.
    success = _run(_decision(aux_role="none", action="success", converged=True))
    candidate = _run(
        _decision(aux_role="none", action="candidate_success", window_ok=True)
    )

    # Then: both reachable positive paths pass.
    assert success.returncode == candidate.returncode == 0


def test_unparseable_json_and_missing_schema_are_usage_errors(tmp_path: Path):
    # Given / When: JSON or schema inputs cannot be loaded.
    malformed = subprocess.run(
        [sys.executable, str(_VERIFY), "--decision", "-"],
        input="{",
        capture_output=True,
        text=True,
        cwd=_REPO_ROOT,
        check=False,
    )
    missing_schema = _run(_decision(), "--schema", str(tmp_path / "missing.json"))

    # Then: boundary failures use reserved exit 2.
    assert malformed.returncode == missing_schema.returncode == 2
    assert "json" in malformed.stderr.lower()
    assert "schema" in missing_schema.stderr.lower()


def test_invalid_utf8_decision_file_is_a_controlled_usage_error(tmp_path: Path):
    # Given: a decision file that cannot be decoded as UTF-8.
    decision_path = tmp_path / "invalid-decision.json"
    decision_path.write_bytes(b"\xff")

    # When: the CLI loads the invalid file through its public boundary.
    result = subprocess.run(
        [sys.executable, str(_VERIFY), "--decision", str(decision_path)],
        capture_output=True,
        text=True,
        cwd=_REPO_ROOT,
        check=False,
    )

    # Then: decoding failures are controlled usage errors, not tracebacks.
    assert result.returncode == 2
    assert result.stdout == ""
    assert "[ERR][verify]" in result.stderr
    assert "decision" in result.stderr.lower()
    assert "traceback" not in result.stderr.lower()


def test_invalid_utf8_schema_file_is_a_controlled_usage_error(tmp_path: Path):
    # Given: a schema file that cannot be decoded as UTF-8.
    schema_path = tmp_path / "invalid-schema.json"
    schema_path.write_bytes(b"\xff")

    # When: the CLI loads the invalid schema through its public boundary.
    result = _run(_decision(), "--schema", str(schema_path))

    # Then: decoding failures are controlled usage errors, not tracebacks.
    assert result.returncode == 2
    assert result.stdout == ""
    assert "[ERR][verify]" in result.stderr
    assert "schema" in result.stderr.lower()
    assert "traceback" not in result.stderr.lower()


def test_structurally_invalid_schema_is_a_controlled_usage_error(tmp_path: Path):
    # Given: syntactically valid JSON that violates the Draft-07 metaschema.
    schema_path = tmp_path / "invalid-schema.json"
    schema_path.write_text(json.dumps({"type": 7}), encoding="utf-8")

    # When: the invalid schema is supplied through the public CLI boundary.
    result = _run(_decision(), "--schema", str(schema_path))

    # Then: schema failure is controlled and never leaks a traceback.
    assert result.returncode == 2
    assert result.stdout == ""
    assert "[ERR][verify]" in result.stderr
    assert "schema" in result.stderr.lower()
    assert "traceback" not in result.stderr.lower()


def test_evidence_path_failure_is_a_controlled_usage_error(tmp_path: Path):
    # Given: a file occupies the parent path required for audit output.
    blocked_parent = tmp_path / "not-a-directory"
    blocked_parent.write_text("blocked", encoding="utf-8")

    # When: an otherwise valid decision requests evidence below that file.
    result = _run(_decision(), "--evidence", str(blocked_parent / "audit.txt"))

    # Then: audit persistence fails closed before any verdict reaches stdout.
    assert result.returncode == 2
    assert result.stdout == ""
    assert "[ERR][verify]" in result.stderr
    assert "evidence" in result.stderr.lower()
    assert "traceback" not in result.stderr.lower()


def test_evidence_is_append_only_for_pass_and_failure(tmp_path: Path):
    # Given: an existing audit file with a sentinel prefix.
    evidence = tmp_path / "audit.txt"
    evidence.write_text("sentinel\n", encoding="utf-8")

    # When: a pass and a failure append audit records.
    passed = _run(_decision(), "--evidence", str(evidence))
    failed = _run(
        _decision(parameter_change_basis={"serial_evidence": []}),
        "--evidence",
        str(evidence),
    )

    # Then: existing bytes remain and both verdicts are recorded.
    audit = evidence.read_text(encoding="utf-8")
    assert passed.returncode == 0 and failed.returncode == 1
    assert audit.startswith("sentinel\n")
    assert audit.count("verify_aux_eye_decision.py audit") == 2
    assert '"verdict": "PASS"' in audit
    assert '"verdict": "FAIL"' in audit


def test_reason_is_never_read_for_behavior():
    # Given: the verifier's parsed source.
    tree = ast.parse(_VERIFY.read_text(encoding="utf-8"))

    # When: all subscripts and mapping reads are inspected.
    reason_reads = [
        node
        for node in ast.walk(tree)
        if (
            isinstance(node, ast.Subscript)
            and isinstance(node.slice, ast.Constant)
            and node.slice.value == "reason"
        )
        or (
            isinstance(node, ast.Call)
            and isinstance(node.func, ast.Attribute)
            and node.func.attr in {"get", "pop", "setdefault"}
            and node.args
            and isinstance(node.args[0], ast.Constant)
            and node.args[0].value == "reason"
        )
    ]

    # Then: reason cannot influence policy or audit behavior.
    assert reason_reads == []
