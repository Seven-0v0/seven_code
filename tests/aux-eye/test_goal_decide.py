from __future__ import annotations

import ast
import hashlib
import json
import subprocess
import sys
import uuid
from pathlib import Path

import pytest


_REPO_ROOT = Path(__file__).resolve().parents[2]
_TOOL = _REPO_ROOT / "tools" / "aux-eye" / "aux_eye_goal_decide.py"
_STATE_TOOL = _REPO_ROOT / "tools" / "aux-eye" / "aux_eye_run_state.py"
_STATE_ROOT = _REPO_ROOT / ".omo" / "evidence" / "aux-eye-monitor"


def _write_goal(path: Path, kind: str, **decision: str | int) -> str:
    payload = {
        "goal_description": "fixture goal",
        "decision": {"kind": kind, **decision},
    }
    path.write_text(json.dumps(payload), encoding="utf-8")
    return hashlib.sha256(path.read_bytes()).hexdigest()


def _observation() -> dict:
    return {
        "frames": [{"path": "0.jpg", "sha256": "0" * 64, "ts": 1}],
        "visible": True,
        "temporal_events": [
            {
                "kind": "oscillation", "status": "detected", "start_frame": 0,
                "end_frame": 0, "trend": "increasing", "confirmations": 2,
                "confidence": 0.8,
                "note": "must remain outside the predicate namespace",
            }
        ],
    }


def _run_state(*arguments: str) -> subprocess.CompletedProcess[str]:
    return subprocess.run(
        [sys.executable, str(_STATE_TOOL), *arguments], capture_output=True,
        text=True, cwd=_REPO_ROOT, check=False,
    )


def _run(
    goal: Path,
    runid: str,
    observation: dict | str | None = None,
    serial: str | None = "[OK][fixture] score=5 mode=ready\n",
    extra: tuple[str, ...] = (),
) -> subprocess.CompletedProcess[str]:
    observation_path = goal.parent / (goal.stem + "-observation.json")
    observation_path.write_text(
        observation if isinstance(observation, str) else json.dumps(observation or _observation()),
        encoding="utf-8",
    )
    serial_path = goal.parent / (goal.stem + "-serial.log")
    arguments = [
        sys.executable, str(_TOOL), "--goal", str(goal),
        "--temporal-observation", str(observation_path), "--runid", runid, *extra,
    ]
    if serial is not None:
        serial_path.write_text(serial, encoding="utf-8")
        arguments.extend(("--serial", str(serial_path)))
    return subprocess.run(
        arguments, capture_output=True, text=True, cwd=_REPO_ROOT, check=False,
    )


@pytest.fixture
def run_state(tmp_path: Path):
    state_goal = tmp_path / "state-goal.json"
    _write_goal(state_goal, "agent_judgment", stability_windows=2)
    runid = _initialize_run(state_goal)

    yield runid

    _remove_run(runid)


def _initialize_run(goal: Path) -> str:
    runid = "goal-%s" % uuid.uuid4().hex
    initialized = _run_state(
        "init", "--runid", runid,
        "--goal-id", hashlib.sha256(goal.read_bytes()).hexdigest(),
        "--goal-path", str(goal), "--camera-index", "1",
        "--camera-name", "fixture-camera", "--serial-device", "/dev/fixture",
        "--serial-baud", "115200", "--max-iterations", "3",
    )
    assert initialized.returncode == 0, initialized.stderr
    return runid


def _remove_run(runid: str) -> None:
    state_dir = _STATE_ROOT / runid
    if state_dir.exists():
        for child in state_dir.iterdir():
            child.unlink()
        state_dir.rmdir()


def test_predicate_goals_use_serial_and_closed_event_namespaces(tmp_path: Path):
    # Given: one observation and two per-run predicates over generic fields.
    matching = tmp_path / "matching.json"
    differing = tmp_path / "differing.json"
    matching_id = _write_goal(
        matching, "predicate",
        predicate='serial.score >= 5 && events.oscillation.status == "detected" && events.oscillation.confirmations >= 2',
        stability_windows=2,
    )
    _write_goal(
        differing, "predicate",
        predicate='serial.score > 9 || events.oscillation.status == "not_detected"',
        stability_windows=2,
    )

    # When: both goals evaluate the same current window.
    matching_run = _initialize_run(matching)
    differing_run = _initialize_run(differing)
    try:
        matched = _run(matching, matching_run)
        differed = _run(differing, differing_run)
    finally:
        _remove_run(matching_run)
        _remove_run(differing_run)

    # Then: the supplied goal controls the predicate result and exact identity.
    assert matched.returncode == 0, matched.stderr
    assert differed.returncode == 0, differed.stderr
    assert json.loads(matched.stdout) == {
        "basis": "predicate", "converged": False,
        "goal_description": "fixture goal", "goal_id": matching_id,
        "goal_predicate_result": True,
    }
    assert json.loads(differed.stdout)["goal_predicate_result"] is False


def test_goal_identity_must_match_authoritative_run_state(tmp_path: Path):
    # Given: a valid but different goal document from the initialized run.
    state_goal = tmp_path / "state-goal.json"
    _write_goal(state_goal, "agent_judgment", stability_windows=2)
    different_goal = tmp_path / "different-goal.json"
    _write_goal(different_goal, "predicate", predicate="serial.score >= 5")

    # When: the goal helper evaluates that different document for the run.
    mismatch_run = _initialize_run(state_goal)
    try:
        result = _run(different_goal, mismatch_run)
    finally:
        _remove_run(mismatch_run)

    # Then: it refuses to attach a decision to the wrong goal identity.
    assert result.returncode == 2
    assert "goal identity mismatch" in result.stderr


def test_predicate_converges_from_authoritative_old_count(tmp_path: Path):
    # Given: one completed stable window in authoritative run-state.
    goal = tmp_path / "predicate.json"
    _write_goal(goal, "predicate", predicate="serial.score >= 5", stability_windows=2)
    runid = _initialize_run(goal)
    try:
        incremented = _run_state("incr-stability", "--runid", runid)
        assert incremented.returncode == 0, incremented.stderr

        # When: the current predicate window is favorable.
        result = _run(goal, runid)

        # Then: old count one plus current window converges without mutating state.
        assert result.returncode == 0, result.stderr
        assert json.loads(result.stdout)["converged"] is True
        state = json.loads(_run_state("get", "--runid", runid).stdout)
        assert state["stability_count"] == 1
    finally:
        _remove_run(runid)


@pytest.mark.parametrize(
    ("old_count", "agent_window_ok", "expected_converged"),
    ((0, "true", False), (1, "true", True), (1, "false", False)),
)
def test_agent_judgment_uses_structured_window_and_stability(
    tmp_path: Path,
    old_count: int,
    agent_window_ok: str,
    expected_converged: bool,
):
    # Given: an agent goal and the requested authoritative prior count.
    goal = tmp_path / ("agent-%s-%s.json" % (old_count, agent_window_ok))
    goal_id = _write_goal(goal, "agent_judgment", stability_windows=2)
    runid = _initialize_run(goal)
    try:
        if old_count:
            assert _run_state("incr-stability", "--runid", runid).returncode == 0

        # When: the agent submits a structured current-window judgment.
        result = _run(goal, runid, extra=("--agent-window-ok", agent_window_ok))

        # Then: agent judgment remains explicit and subject to the same window gate.
        assert result.returncode == 0, result.stderr
        assert json.loads(result.stdout) == {
            "agent_window_ok": agent_window_ok == "true", "basis": "agent_judgment",
            "converged": expected_converged, "goal_description": "fixture goal",
            "goal_id": goal_id, "goal_predicate_result": None,
        }
    finally:
        _remove_run(runid)


def test_agent_judgment_requires_agent_window_ok(tmp_path: Path):
    # Given: a schema-valid agent judgment goal.
    goal = tmp_path / "agent.json"
    _write_goal(goal, "agent_judgment", stability_windows=2)
    runid = _initialize_run(goal)
    try:
        # When: no structured agent result is supplied.
        result = _run(goal, runid)

        # Then: the boundary rejects the incomplete request without a traceback.
        assert result.returncode == 2
        assert "agent-window-ok" in result.stderr
        assert "Traceback" not in result.stderr
    finally:
        _remove_run(runid)


@pytest.mark.parametrize(
    ("goal_payload", "observation", "stderr_fragment"),
    (
        ({"decision": {"kind": "agent_judgment"}}, None, "goal"),
        ({"goal_description": "x"}, None, "goal"),
        (None, "{", "temporal"),
    ),
)
def test_malformed_boundary_inputs_exit_two_without_traceback(
    tmp_path: Path,
    run_state: str,
    goal_payload: dict | None,
    observation: str | None,
    stderr_fragment: str,
):
    # Given: malformed JSON/schema input at one CLI boundary.
    goal = tmp_path / ("bad-%s.json" % uuid.uuid4().hex)
    if goal_payload is None:
        _write_goal(goal, "predicate", predicate="serial.score > 0")
    else:
        goal.write_text(json.dumps(goal_payload), encoding="utf-8")

    # When: goal decision is requested.
    result = _run(goal, run_state, observation=observation)

    # Then: the tool reports a controlled usage error.
    assert result.returncode == 2
    assert stderr_fragment in result.stderr.lower()
    assert "Traceback" not in result.stderr


def test_predicate_requires_predicate_and_rejects_agent_flag(tmp_path: Path):
    # Given: predicate goals with incomplete or contradictory mode input.
    missing = tmp_path / "missing-predicate.json"
    contradictory = tmp_path / "contradictory.json"
    _write_goal(missing, "predicate", stability_windows=2)
    _write_goal(contradictory, "predicate", predicate="serial.score > 0")

    missing_run = _initialize_run(missing)
    contradictory_run = _initialize_run(contradictory)
    try:
        # When: each invalid request reaches the CLI boundary.
        missing_result = _run(missing, missing_run)
        contradictory_result = _run(
            contradictory, contradictory_run, extra=("--agent-window-ok", "true")
        )

        # Then: neither mode ambiguity reaches evaluation.
        assert missing_result.returncode == 2
        assert "predicate" in missing_result.stderr
        assert contradictory_result.returncode == 2
        assert "agent-window-ok" in contradictory_result.stderr
    finally:
        _remove_run(missing_run)
        _remove_run(contradictory_run)


@pytest.mark.parametrize(
    ("goal_source", "observation_source", "serial_source"),
    (("path", "-", "-"), ("-", "-", "path"), ("-", "path", "-")),
)
def test_dual_stdin_sources_are_rejected_without_consuming_input(
    tmp_path: Path,
    run_state: str,
    goal_source: str,
    observation_source: str,
    serial_source: str,
):
    # Given: two stream-capable arguments point at the one stdin stream.
    goal = tmp_path / "goal.json"
    _write_goal(goal, "predicate", predicate="serial.score > 0")
    observation = tmp_path / "observation.json"
    observation.write_text(json.dumps(_observation()), encoding="utf-8")
    serial = tmp_path / "serial.log"
    serial.write_text("[OK][fixture] score=5\n", encoding="utf-8")

    # When: the ambiguous request is invoked.
    result = subprocess.run(
        [
            sys.executable, str(_TOOL), "--goal",
            "-" if goal_source == "-" else str(goal), "--temporal-observation",
            "-" if observation_source == "-" else str(observation), "--serial",
            "-" if serial_source == "-" else str(serial), "--runid", run_state,
        ],
        input=json.dumps(_observation()), capture_output=True, text=True,
        cwd=_REPO_ROOT, check=False,
    )

    # Then: argparse-style usage failure identifies the stdin conflict.
    assert result.returncode == 2
    assert "stdin" in result.stderr.lower()
    assert "Traceback" not in result.stderr


def test_invalid_predicate_and_missing_run_state_are_controlled(tmp_path: Path):
    # Given: one syntax-invalid goal and one nonexistent authoritative run.
    bad_predicate = tmp_path / "bad-predicate.json"
    valid_predicate = tmp_path / "valid-predicate.json"
    _write_goal(bad_predicate, "predicate", predicate="serial.score >")
    _write_goal(valid_predicate, "predicate", predicate="serial.score > 0")

    runid = _initialize_run(bad_predicate)
    try:
        # When: each failure path is exercised.
        syntax_result = _run(bad_predicate, runid)
        missing_state = _run(valid_predicate, "missing-%s" % uuid.uuid4().hex)

        # Then: evaluator and state failures stay within exit-code 2.
        assert syntax_result.returncode == 2
        assert "predicate" in syntax_result.stderr
        assert "Traceback" not in syntax_result.stderr
        assert missing_state.returncode == 2
        assert "state" in missing_state.stderr
        assert "Traceback" not in missing_state.stderr
    finally:
        _remove_run(runid)


def test_help_source_purity_and_generic_contract():
    # Given: the goal decision source parsed structurally.
    source = _TOOL.read_text(encoding="utf-8")
    tree = ast.parse(source)

    # When: help, imports, and direct calls are inspected.
    result = subprocess.run(
        [sys.executable, str(_TOOL), "--help"], capture_output=True,
        text=True, cwd=_REPO_ROOT, check=False,
    )
    imported_roots = {
        imported_name.split(".", maxsplit=1)[0]
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
        for node in ast.walk(tree)
        if isinstance(node, ast.Call) and isinstance(node.func, ast.Name)
    }

    # Then: the CLI is available with no network, dynamic evaluation, or device defaults.
    assert result.returncode == 0
    assert "--agent-window-ok" in result.stdout
    assert imported_roots.isdisjoint({"anthropic", "httpx", "openai", "requests", "socket", "urllib"})
    assert direct_calls.isdisjoint({"eval", "exec"})
    assert not any(
        token in source.lower()
        for token in ("tilt", "wheel", "rpm", "kp", "ki", "kd", "平衡车")
    )
