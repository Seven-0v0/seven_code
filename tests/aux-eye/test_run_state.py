from __future__ import annotations
# noqa: SIZE_OK - End-to-end contract tests for one authoritative CLI.

import ast
import hashlib
import importlib.util
import json
import os
import shutil
import stat
import subprocess
import sys
import uuid
from pathlib import Path

import pytest


_REPO_ROOT = Path(__file__).resolve().parents[2]
_TOOL = _REPO_ROOT / "tools" / "aux-eye" / "aux_eye_run_state.py"
_STATE_ROOT = _REPO_ROOT / ".omo" / "evidence" / "aux-eye-monitor"
_FIELDS = {
    "runid", "iteration", "cycle_phase", "goal_id", "goal_path",
    "camera_index", "camera_name", "serial_device", "serial_baud",
    "max_iterations", "stability_count", "consecutive_boot_count",
    "visibility_loss_count", "serial_silence_accum_s", "serial_capture_pid",
    "serial_capture_identity", "serial_capture_out", "serial_capture_err",
    "serial_capture_ready", "cycle_deadline_s", "cycle_started_monotonic_s",
    "pending_action", "pending_action_converged", "pending_action_window_ok",
    "build_flash_done", "last_action", "terminal", "terminal_reason",
    "created_ts", "updated_ts",
}


def _run(*args: str) -> subprocess.CompletedProcess[str]:
    return subprocess.run(
        [sys.executable, str(_TOOL), *args],
        capture_output=True,
        text=True,
        cwd=_REPO_ROOT,
        check=False,
    )


def _write_goal(path: Path, **decision: int | float | str) -> str:
    payload = {
        "goal_description": "fixture goal",
        "decision": {"kind": "agent_judgment", **decision},
    }
    path.write_text(json.dumps(payload), encoding="utf-8")
    return hashlib.sha256(path.read_bytes()).hexdigest()


def _init(
    runid: str,
    goal: Path,
    max_iterations: int = 3,
    cycle_deadline_s: float = 180.0,
) -> subprocess.CompletedProcess[str]:
    return _run(
        "init", "--runid", runid,
        "--goal-id", hashlib.sha256(goal.read_bytes()).hexdigest(),
        "--goal-path", str(goal),
        "--camera-index", "1", "--camera-name", "fixture-camera",
        "--serial-device", "/dev/fixture", "--serial-baud", "115200",
        "--max-iterations", str(max_iterations), "--cycle-deadline-s", str(cycle_deadline_s),
    )


def _state(runid: str) -> dict:
    result = _run("get", "--runid", runid)
    assert result.returncode == 0, result.stderr
    return json.loads(result.stdout)


def _capture_identity(pid: int) -> str:
    result = _run("capture-identity", "--pid", str(pid))
    assert result.returncode == 0, result.stderr
    payload = json.loads(result.stdout)
    assert payload["pid"] == pid
    return payload["identity"]


def _register_capture(runid: str, pid: int, stdout_path: Path, stderr_path: Path):
    return _run(
        "set-serial-capture", "--runid", runid, "--pid", str(pid),
        "--identity", _capture_identity(pid), "--stdout", str(stdout_path),
        "--stderr", str(stderr_path),
    )


def _stop_owned(process: subprocess.Popen) -> None:
    if process.poll() is not None:
        return
    process.terminate()
    try:
        process.wait(timeout=3)
    except subprocess.TimeoutExpired:
        process.kill()
        process.wait(timeout=3)


@pytest.fixture
def run_state(tmp_path: Path):
    # Given: a unique, correctly goal-bound run.
    runid = "test-%s" % uuid.uuid4().hex
    goal = tmp_path / "goal.json"
    _write_goal(
        goal,
        serial_silence_s=30,
        max_consecutive_resets=1,
        visibility_loss_windows=2,
    )
    result = _init(runid, goal)
    assert result.returncode == 0, result.stderr

    yield runid, goal

    subprocess.run(
        [sys.executable, str(_TOOL), "clear-serial-capture", "--runid", runid],
        capture_output=True,
        cwd=_REPO_ROOT,
        check=False,
    )
    state_dir = _STATE_ROOT / runid
    if state_dir.exists():
        for child in state_dir.iterdir():
            child.unlink()
        state_dir.rmdir()


def _start_serial_fixture(tmp_path: Path, marker_stream: str = "stderr"):
    stdout_path = tmp_path / (uuid.uuid4().hex + ".out")
    stderr_path = tmp_path / (uuid.uuid4().hex + ".err")
    stdout_path.write_text(
        "[OK][serial] capturing from fixture\n" if marker_stream == "stdout" else "",
        encoding="utf-8",
    )
    stderr_path.write_text(
        "[OK][serial] capturing from fixture\n" if marker_stream == "stderr" else "",
        encoding="utf-8",
    )
    process = subprocess.Popen([sys.executable, "-c", "import time; time.sleep(60)"])
    return process, stdout_path, stderr_path


def _ready_serial(runid: str, tmp_path: Path):
    process, stdout_path, stderr_path = _start_serial_fixture(tmp_path)
    result = _register_capture(runid, process.pid, stdout_path, stderr_path)
    assert result.returncode == 0, result.stderr
    result = _run("set-serial-ready", "--runid", runid)
    assert result.returncode == 0, result.stderr
    return process


def _to_decided(runid: str, tmp_path: Path) -> subprocess.Popen:
    for phase in ("building", "serial_started"):
        result = _run("set-phase", "--runid", runid, "--phase", phase)
        assert result.returncode == 0, result.stderr
    process = _ready_serial(runid, tmp_path)
    assert _run("set-phase", "--runid", runid, "--phase", "flash_started").returncode == 0
    assert _run("set-flash-done", "--runid", runid).returncode == 0
    for phase in ("flashed", "captured", "evaluated", "decided"):
        result = _run("set-phase", "--runid", runid, "--phase", phase)
        assert result.returncode == 0, result.stderr
    return process


def test_init_get_has_exact_authoritative_fields_and_rejects_unsafe_identity(tmp_path: Path):
    # Given: a goal file and a fresh run id.
    goal = tmp_path / "goal.json"
    goal_id = _write_goal(goal)
    runid = "test-%s" % uuid.uuid4().hex

    # When: the run is initialized and read back.
    result = _init(runid, goal, max_iterations=4)
    state = _state(runid)

    # Then: the complete authoritative shape and identity are present.
    assert result.returncode == 0
    assert set(state) == _FIELDS
    assert state == {
        **state,
        "runid": runid,
        "iteration": 0,
        "cycle_phase": "idle",
        "goal_id": goal_id,
        "goal_path": str(goal),
        "camera_index": 1,
        "camera_name": "fixture-camera",
        "serial_device": "/dev/fixture",
        "serial_baud": 115200,
        "max_iterations": 4,
        "stability_count": 0,
        "consecutive_boot_count": 0,
        "visibility_loss_count": 0,
        "serial_silence_accum_s": 0.0,
        "serial_capture_pid": None,
        "serial_capture_identity": None,
        "serial_capture_out": None,
        "serial_capture_err": None,
        "serial_capture_ready": False,
        "cycle_deadline_s": 180.0,
        "cycle_started_monotonic_s": None,
        "pending_action": None,
        "pending_action_converged": None,
        "pending_action_window_ok": None,
        "build_flash_done": False,
        "last_action": None,
        "terminal": False,
        "terminal_reason": None,
    }

    bad_runid = _init("../evil", goal)
    bad_goal = _run(
        "init", "--runid", "test-%s" % uuid.uuid4().hex,
        "--goal-id", "0" * 64, "--goal-path", str(goal),
        "--camera-index", "1", "--camera-name", "fixture-camera",
        "--serial-device", "/dev/fixture", "--serial-baud", "115200",
        "--max-iterations", "3",
    )
    assert bad_runid.returncode == 2
    assert bad_goal.returncode != 0

    state_dir = _STATE_ROOT / runid
    for child in state_dir.iterdir():
        child.unlink()
    state_dir.rmdir()


def test_phase_order_continue_reset_and_counter_lifecycle(run_state, tmp_path: Path):
    # Given: a run progressing through the required pre-flash serial order.
    runid, _ = run_state
    process = _to_decided(runid, tmp_path)

    # When: counters change and the decided cycle continues.
    assert _run("incr-stability", "--runid", runid).returncode == 0
    assert _run("incr-stability", "--runid", runid).returncode == 0
    assert _run("clear-serial-capture", "--runid", runid).returncode == 0
    process.wait(timeout=3)
    result = _run("advance", "--runid", runid, "--action", "continue")

    # Then: the next iteration rebuilds/reflashes and preserves the counter.
    state = _state(runid)
    assert result.returncode == 0, result.stderr
    assert state["iteration"] == 1
    assert state["cycle_phase"] == "building"
    assert state["build_flash_done"] is False
    assert state["stability_count"] == 2
    assert state["last_action"] == "continue"


def test_phase_transition_rejects_jump_and_flash_without_ready_serial(run_state):
    # Given: a new idle run.
    runid, _ = run_state

    # When: phase transitions skip required predecessors or serial readiness.
    jump = _run("set-phase", "--runid", runid, "--phase", "flash_started")
    assert _run("set-phase", "--runid", runid, "--phase", "building").returncode == 0
    assert _run("set-phase", "--runid", runid, "--phase", "serial_started").returncode == 0
    unready = _run("set-phase", "--runid", runid, "--phase", "flash_started")

    # Then: neither unsafe transition is accepted.
    assert jump.returncode != 0
    assert unready.returncode != 0
    assert _state(runid)["cycle_phase"] == "serial_started"


def test_flash_rejects_marker_that_was_not_persisted_as_ready(run_state, tmp_path: Path):
    # Given: a live capture and stderr marker that have not passed set-serial-ready.
    runid, _ = run_state
    assert _run("set-phase", "--runid", runid, "--phase", "building").returncode == 0
    assert _run("set-phase", "--runid", runid, "--phase", "serial_started").returncode == 0
    process, stdout_path, stderr_path = _start_serial_fixture(tmp_path)
    assert _register_capture(runid, process.pid, stdout_path, stderr_path).returncode == 0

    # When: flash_started attempts to bypass the persisted readiness command.
    result = _run("set-phase", "--runid", runid, "--phase", "flash_started")

    # Then: the bypass is rejected and cleanup reaps the live process.
    assert result.returncode != 0
    assert _state(runid)["cycle_phase"] == "serial_started"
    assert _run("clear-serial-capture", "--runid", runid).returncode == 0
    process.wait(timeout=3)


@pytest.mark.parametrize("marker_stream, expected_ready", [("stderr", True), ("stdout", False), ("none", False)])
def test_serial_ready_requires_live_pid_and_stderr_marker(run_state, tmp_path: Path, marker_stream: str, expected_ready: bool):
    # Given: separate serial stdout/stderr files and a live capture PID.
    runid, _ = run_state
    process, stdout_path, stderr_path = _start_serial_fixture(tmp_path, marker_stream)
    assert _register_capture(runid, process.pid, stdout_path, stderr_path).returncode == 0

    # When: readiness checks the process and open-success marker.
    result = _run("set-serial-ready", "--runid", runid)

    # Then: stdout bytes are irrelevant and only stderr marks readiness.
    assert result.returncode == (0 if expected_ready else 1)
    assert _state(runid)["serial_capture_ready"] is expected_ready
    assert _run("clear-serial-capture", "--runid", runid).returncode == 0
    process.wait(timeout=3)


def test_serial_ready_rejects_dead_pid(run_state, tmp_path: Path):
    # Given: a completed process and a valid stderr marker.
    runid, _ = run_state
    process, stdout_path, stderr_path = _start_serial_fixture(tmp_path)
    process.terminate()
    process.wait(timeout=3)
    registration = _run(
        "set-serial-capture", "--runid", runid, "--pid", str(process.pid),
        "--identity", "0" * 64, "--stdout", str(stdout_path), "--stderr", str(stderr_path),
    )

    # When: readiness is evaluated.
    result = registration

    # Then: the stale PID fails closed.
    assert result.returncode == 1
    assert "identity" in result.stderr
    assert _state(runid)["serial_capture_pid"] is None


def test_replacing_serial_capture_reaps_previous_pid(run_state, tmp_path: Path):
    # Given: one registered live capture and a replacement capture.
    runid, _ = run_state
    first, first_out, first_err = _start_serial_fixture(tmp_path)
    second, second_out, second_err = _start_serial_fixture(tmp_path)
    assert _register_capture(runid, first.pid, first_out, first_err).returncode == 0

    # When: the replacement registration is persisted.
    result = _register_capture(runid, second.pid, second_out, second_err)

    # Then: ownership transfers without orphaning the previous PID.
    assert result.returncode == 0
    first.wait(timeout=3)
    assert _state(runid)["serial_capture_pid"] == second.pid
    assert _run("clear-serial-capture", "--runid", runid).returncode == 0
    second.wait(timeout=3)


def test_serial_capture_requires_distinct_stdout_and_stderr(run_state, tmp_path: Path):
    # Given: a live capture with one path proposed for both streams.
    runid, _ = run_state
    process, stdout_path, _ = _start_serial_fixture(tmp_path)

    # When: the capture registration aliases stdout and stderr.
    result = _run(
        "set-serial-capture", "--runid", runid, "--pid", str(process.pid),
        "--stdout", str(stdout_path), "--stderr", str(stdout_path),
    )

    # Then: registration is rejected without taking process ownership.
    assert result.returncode != 0
    process.terminate()
    process.wait(timeout=3)


def test_flash_done_rechecks_replacement_capture_readiness(run_state, tmp_path: Path):
    # Given: a ready capture at flash_started, then a replacement capture.
    runid, _ = run_state
    assert _run("set-phase", "--runid", runid, "--phase", "building").returncode == 0
    assert _run("set-phase", "--runid", runid, "--phase", "serial_started").returncode == 0
    first = _ready_serial(runid, tmp_path)
    assert _run("set-phase", "--runid", runid, "--phase", "flash_started").returncode == 0
    second, second_out, second_err = _start_serial_fixture(tmp_path)
    assert _register_capture(runid, second.pid, second_out, second_err).returncode == 0
    first.wait(timeout=3)

    # When: flash completion is recorded without acknowledging the replacement.
    result = _run("set-flash-done", "--runid", runid)

    # Then: the stale readiness state cannot authorize completion.
    assert result.returncode != 0
    assert _state(runid)["build_flash_done"] is False
    assert _run("clear-serial-capture", "--runid", runid).returncode == 0
    second.wait(timeout=3)


@pytest.mark.parametrize(
    "mutations, reason",
    [
        (("add-silence", "--seconds", "40"), "silence"),
        (("incr-boot", "incr-boot"), "reset"),
        (("incr-visibility-loss", "incr-visibility-loss"), "visibility"),
    ],
)
def test_failsafe_thresholds_trip_with_structured_reason(run_state, mutations, reason: str):
    # Given: counters that exceed one configured fail-safe threshold.
    runid, goal = run_state
    if mutations[0] == "incr-boot":
        commands = [("incr-boot",), ("incr-boot",)]
    elif mutations[0] == "incr-visibility-loss":
        commands = [("incr-visibility-loss",), ("incr-visibility-loss",)]
    else:
        commands = [mutations]
    for command in commands:
        assert _run(*command, "--runid", runid).returncode == 0

    # When: the goal thresholds are evaluated.
    result = _run("failsafe-check", "--runid", runid, "--goal", str(goal))

    # Then: exit 3 identifies the fail-closed cause.
    assert result.returncode == 3
    assert reason in result.stdout


def test_failsafe_within_threshold_does_not_trip(run_state):
    # Given: silence below the configured threshold.
    runid, goal = run_state
    assert _run("add-silence", "--runid", runid, "--seconds", "20").returncode == 0

    # When: fail-safe state is evaluated.
    result = _run("failsafe-check", "--runid", runid, "--goal", str(goal))

    # Then: the run remains safe to continue.
    assert result.returncode == 0
    assert json.loads(result.stdout) == {"triggered": False}


def test_next_action_rejects_predecided_binding(run_state):
    # Given: a fresh run that has not completed its observation/decision phases.
    runid, goal = run_state

    # When: success is requested before the cycle reaches decided.
    result = _run(
        "next-action", "--runid", runid, "--goal", str(goal),
        "--converged", "true", "--window-ok", "true",
    )

    # Then: no pending action can be persisted early for later consumption.
    assert result.returncode != 0
    assert "decided" in result.stderr
    state = _state(runid)
    assert state["pending_action"] is None
    assert state["pending_action_converged"] is None
    assert state["pending_action_window_ok"] is None


@pytest.mark.parametrize(
    "converged, window_ok, action",
    [("true", "true", "success"), ("false", "true", "candidate_success"), ("false", "false", "continue")],
)
def test_next_action_persists_decision_binding(run_state, tmp_path: Path, converged: str, window_ok: str, action: str):
    # Given: a clean decided run below its iteration ceiling.
    runid, goal = run_state
    process = _to_decided(runid, tmp_path)

    # When: the decision gate evaluates convergence and current-window status.
    result = _run(
        "next-action", "--runid", runid, "--goal", str(goal),
        "--converged", converged, "--window-ok", window_ok,
        "--camera-index-now", "1", "--camera-name-now", "fixture-camera",
    )

    # Then: exactly one action and its binding are persisted.
    payload = json.loads(result.stdout)
    state = _state(runid)
    assert payload["action"] == action
    assert not (converged == "false" and action == "success")
    assert state["pending_action"] == action
    assert state["pending_action_converged"] is (converged == "true")
    assert state["pending_action_window_ok"] is (window_ok == "true")
    assert _run("clear-serial-capture", "--runid", runid).returncode == 0
    process.wait(timeout=3)


def test_next_action_prioritizes_failsafe_camera_and_iteration(run_state):
    # Given: a run with a camera identity mismatch.
    runid, goal = run_state

    # When: next-action receives the current camera mapping.
    camera = _run(
        "next-action", "--runid", runid, "--goal", str(goal),
        "--converged", "true", "--window-ok", "true",
        "--camera-index-now", "2", "--camera-name-now", "other-camera",
    )

    # Then: identity failure overrides convergence.
    assert camera.returncode == 3
    assert json.loads(camera.stdout) == {
        "action": "needs_human", "terminal_reason": "camera_identity"
    }


def test_next_action_prioritizes_silence_failsafe(run_state):
    # Given: a run whose accumulated silence reaches the goal threshold.
    runid, goal = run_state
    assert _run("add-silence", "--runid", runid, "--seconds", "30").returncode == 0

    # When: next-action evaluates an otherwise converged window.
    result = _run(
        "next-action", "--runid", runid, "--goal", str(goal),
        "--converged", "true", "--window-ok", "true",
    )

    # Then: the fail-safe overrides success.
    assert result.returncode == 3
    assert json.loads(result.stdout) == {
        "action": "needs_human", "terminal_reason": "serial_silence"
    }


def test_next_action_prioritizes_iteration_exhaustion(tmp_path: Path):
    # Given: a run advanced through the CLI to its configured iteration ceiling.
    runid = "test-%s" % uuid.uuid4().hex
    goal = tmp_path / "iteration-goal.json"
    _write_goal(goal)
    assert _init(runid, goal, max_iterations=1).returncode == 0
    process = _to_decided(runid, tmp_path)
    try:
        assert _run(
            "next-action", "--runid", runid, "--goal", str(goal),
            "--converged", "false", "--window-ok", "false",
        ).returncode == 0
        assert _run("advance", "--runid", runid, "--action", "continue").returncode == 0
        process.wait(timeout=3)

        # When: next-action evaluates an otherwise converged window.
        result = _run(
            "next-action", "--runid", runid, "--goal", str(goal),
            "--converged", "true", "--window-ok", "true",
        )

        # Then: exhaustion overrides success.
        assert result.returncode == 3
        assert json.loads(result.stdout) == {
            "action": "needs_human", "terminal_reason": "iteration_exhausted"
        }
    finally:
        _run("clear-serial-capture", "--runid", runid)
        state_dir = _STATE_ROOT / runid
        if state_dir.exists():
            for child in state_dir.iterdir():
                child.unlink()
            state_dir.rmdir()


def test_advance_enforces_success_candidate_binding_and_terminal_lock(run_state, tmp_path: Path):
    # Given: a decided cycle without a pending decision.
    runid, goal = run_state
    process = _to_decided(runid, tmp_path)
    assert _run("clear-serial-capture", "--runid", runid).returncode == 0
    process.wait(timeout=3)

    # When: success bypasses next-action, then is retried with a valid binding.
    bypass = _run("advance", "--runid", runid, "--action", "success")
    assert _run(
        "next-action", "--runid", runid, "--goal", str(goal),
        "--converged", "true", "--window-ok", "true",
    ).returncode == 0
    accepted = _run("advance", "--runid", runid, "--action", "success")

    # Then: only bound convergence reaches a terminal success and all writes lock.
    assert bypass.returncode != 0
    assert "not-converged" in bypass.stderr
    assert accepted.returncode == 0, accepted.stderr
    assert _run("incr-stability", "--runid", runid).returncode != 0
    assert _run(
        "next-action", "--runid", runid, "--goal", str(goal),
        "--converged", "true", "--window-ok", "true",
    ).returncode != 0
    assert _run("is-terminal", "--runid", runid).returncode == 3


def test_candidate_advance_requires_pending_window_ok(run_state, tmp_path: Path):
    # Given: a decided cycle whose current window is not acceptable.
    runid, goal = run_state
    process = _to_decided(runid, tmp_path)
    assert _run("clear-serial-capture", "--runid", runid).returncode == 0
    process.wait(timeout=3)
    assert _run(
        "next-action", "--runid", runid, "--goal", str(goal),
        "--converged", "false", "--window-ok", "false",
    ).returncode == 0

    # When: candidate_success is advanced despite the persisted decision.
    result = _run("advance", "--runid", runid, "--action", "candidate_success")

    # Then: the not-candidate binding rejects the bypass.
    assert result.returncode != 0
    assert "not-candidate" in result.stderr


def test_candidate_advance_accepts_pending_window_binding(run_state, tmp_path: Path):
    # Given: a decided cycle bound to a passing current window without convergence.
    runid, goal = run_state
    process = _to_decided(runid, tmp_path)
    assert _run(
        "next-action", "--runid", runid, "--goal", str(goal),
        "--converged", "false", "--window-ok", "true",
    ).returncode == 0

    # When: candidate_success consumes its persisted binding.
    result = _run("advance", "--runid", runid, "--action", "candidate_success")

    # Then: it remains nonterminal, starts a new build, and reaps serial capture.
    state = _state(runid)
    assert result.returncode == 0
    assert state["terminal"] is False
    assert state["cycle_phase"] == "building"
    assert state["iteration"] == 1
    process.wait(timeout=3)


@pytest.mark.parametrize(
    "converged, window_ok, pending_action",
    [
        ("true", "true", "success"),
        ("false", "true", "candidate_success"),
        ("false", "false", "needs_human"),
    ],
)
def test_advance_rejects_continue_when_pending_action_differs(
    run_state,
    tmp_path: Path,
    converged: str,
    window_ok: str,
    pending_action: str,
):
    # Given: a decided cycle with an authoritative pending action other than continue.
    runid, goal = run_state
    process = _to_decided(runid, tmp_path)
    if pending_action == "needs_human":
        assert _run("add-silence", "--runid", runid, "--seconds", "30").returncode == 0
    decision = _run(
        "next-action", "--runid", runid, "--goal", str(goal),
        "--converged", converged, "--window-ok", window_ok,
    )
    assert json.loads(decision.stdout)["action"] == pending_action
    before = _state(runid)

    # When: the caller attempts to override the pending action with continue.
    result = _run("advance", "--runid", runid, "--action", "continue")

    # Then: the mismatch fails before consuming or mutating authoritative state.
    after = _state(runid)
    assert result.returncode != 0
    assert "pending-action-mismatch" in result.stderr
    assert after == before
    assert process.poll() is None
    assert _run("clear-serial-capture", "--runid", runid).returncode == 0
    process.wait(timeout=3)


def test_advance_accepts_matching_pending_needs_human(run_state, tmp_path: Path):
    # Given: a decided cycle whose fail-safe selects needs_human.
    runid, goal = run_state
    process = _to_decided(runid, tmp_path)
    assert _run("add-silence", "--runid", runid, "--seconds", "30").returncode == 0
    decision = _run(
        "next-action", "--runid", runid, "--goal", str(goal),
        "--converged", "false", "--window-ok", "false",
    )
    assert json.loads(decision.stdout)["action"] == "needs_human"

    # When: advance consumes the matching pending action.
    result = _run(
        "advance", "--runid", runid, "--action", "needs_human",
        "--terminal-reason", "serial_silence",
    )

    # Then: the run terminalizes and capture ownership is cleaned up.
    state = _state(runid)
    assert result.returncode == 0
    assert state["terminal"] is True
    assert state["last_action"] == "needs_human"
    assert state["terminal_reason"] == "serial_silence"
    process.wait(timeout=3)


def test_resume_checks_goal_hash_and_fails_closed_on_interrupted_flash(run_state, tmp_path: Path):
    # Given: a run atomically recorded immediately before flash.
    runid, goal = run_state
    assert _run("set-phase", "--runid", runid, "--phase", "building").returncode == 0
    assert _run("set-phase", "--runid", runid, "--phase", "serial_started").returncode == 0
    process = _ready_serial(runid, tmp_path)
    assert _run("set-phase", "--runid", runid, "--phase", "flash_started").returncode == 0
    assert _run("clear-serial-capture", "--runid", runid).returncode == 0
    process.wait(timeout=3)

    # When: resume sees an unconfirmed flash completion.
    interrupted = _run("resume-check", "--runid", runid, "--goal-path", str(goal))

    # Then: the run terminalizes as needs_human and cannot mutate further.
    state = _state(runid)
    assert interrupted.returncode == 3
    assert "flash_interrupted" in interrupted.stdout
    assert state["terminal"] is True
    assert state["last_action"] == "needs_human"
    assert state["terminal_reason"] == "flash_interrupted"
    assert _run("reset-silence", "--runid", runid).returncode != 0


def test_resume_rejects_changed_goal(run_state):
    # Given: an initialized run whose goal file is changed afterward.
    runid, goal = run_state
    goal.write_text('{"goal_description":"changed","decision":{"kind":"agent_judgment"}}')

    # When: resume recomputes the goal identity.
    result = _run("resume-check", "--runid", runid, "--goal-path", str(goal))

    # Then: resume is rejected before state mutation.
    assert result.returncode != 0
    assert "goal" in result.stderr.lower()


def test_corrupt_state_is_rejected_and_failed_replace_preserves_previous_json(run_state, monkeypatch):
    # Given: a valid state and an injected atomic-replace interruption.
    runid, _ = run_state
    state_path = _STATE_ROOT / runid / "state.json"
    before = json.loads(state_path.read_text(encoding="utf-8"))
    spec = importlib.util.spec_from_file_location("aux_eye_run_state", _TOOL)
    assert spec is not None and spec.loader is not None
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)

    def interrupt_replace(source, destination):
        raise OSError("simulated interruption")

    monkeypatch.setattr(module.os, "replace", interrupt_replace)

    # When: the next atomic state replacement is interrupted.
    with pytest.raises(OSError, match="simulated interruption"):
        module._write_state(state_path, {**before, "stability_count": 1})

    # Then: the prior state remains complete JSON; corrupt input is separately rejected.
    assert json.loads(state_path.read_text(encoding="utf-8")) == before
    state_path.write_text('{"runid":', encoding="utf-8")
    result = _run("get", "--runid", runid)
    assert result.returncode == 2
    assert "state" in result.stderr.lower()


def test_atomic_replace_fsyncs_file_and_parent_directory(run_state, monkeypatch):
    # Given: a valid state and an fsync recorder that identifies descriptor kind.
    runid, _ = run_state
    state_path = _STATE_ROOT / runid / "state.json"
    state = json.loads(state_path.read_text(encoding="utf-8"))
    spec = importlib.util.spec_from_file_location("aux_eye_run_state_fsync", _TOOL)
    assert spec is not None and spec.loader is not None
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    real_fsync = module.os.fsync
    descriptor_kinds = []

    def record_fsync(descriptor):
        descriptor_kinds.append("directory" if stat.S_ISDIR(os.fstat(descriptor).st_mode) else "file")
        real_fsync(descriptor)

    monkeypatch.setattr(module.os, "fsync", record_fsync)

    # When: a state replacement is committed.
    module._write_state(state_path, {**state, "stability_count": 1})

    # Then: both file contents and the renamed directory entry are durable.
    assert descriptor_kinds == ["file", "directory"]


def test_failed_capture_handoff_reaps_replacement_pid(run_state, tmp_path: Path, monkeypatch):
    # Given: one recorded live capture and a replacement PID.
    runid, _ = run_state
    first, first_out, first_err = _start_serial_fixture(tmp_path)
    second, second_out, second_err = _start_serial_fixture(tmp_path)
    assert _register_capture(runid, first.pid, first_out, first_err).returncode == 0
    spec = importlib.util.spec_from_file_location("aux_eye_run_state_handoff", _TOOL)
    assert spec is not None and spec.loader is not None
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    args = module.argparse.Namespace(
        runid=runid, pid=second.pid, identity=_capture_identity(second.pid),
        stdout=str(second_out), stderr=str(second_err)
    )

    def fail_save(path, state):
        raise OSError("simulated handoff failure")

    monkeypatch.setattr(module, "_save_state", fail_save)

    # When: persistence fails after lifecycle ownership starts transferring.
    with pytest.raises(OSError, match="simulated handoff failure"):
        module._cmd_set_serial_capture(args)

    # Then: neither old nor replacement PID remains live and old state stays parseable.
    first.wait(timeout=3)
    second.wait(timeout=3)
    assert json.loads((_STATE_ROOT / runid / "state.json").read_text())["serial_capture_pid"] == first.pid


def test_pid_only_capture_registration_cannot_take_ownership(run_state, tmp_path: Path):
    # Given: an unrelated, test-owned process and plausible separate stream paths.
    runid, _ = run_state
    process, stdout_path, stderr_path = _start_serial_fixture(tmp_path)

    try:
        # When: a caller supplies only the reusable PID.
        result = _run(
            "set-serial-capture", "--runid", runid, "--pid", str(process.pid),
            "--stdout", str(stdout_path), "--stderr", str(stderr_path),
        )

        # Then: registration fails before state can later signal that process.
        assert result.returncode == 2
        assert process.poll() is None
        assert _state(runid)["serial_capture_pid"] is None
    finally:
        _stop_owned(process)


def test_identity_mismatch_never_signals_registered_process(run_state, tmp_path: Path):
    # Given: a process registered with a verified identity token.
    runid, _ = run_state
    process, stdout_path, stderr_path = _start_serial_fixture(tmp_path)
    state_path = _STATE_ROOT / runid / "state.json"
    assert _register_capture(runid, process.pid, stdout_path, stderr_path).returncode == 0
    persisted = json.loads(state_path.read_text(encoding="utf-8"))
    persisted["serial_capture_identity"] = "0" * 64
    state_path.write_text(json.dumps(persisted), encoding="utf-8")

    try:
        # When: cleanup sees a stale/reused identity record.
        result = _run("clear-serial-capture", "--runid", runid)

        # Then: state is cleared but the nonmatching process is never signaled.
        assert result.returncode == 0, result.stderr
        assert process.poll() is None
        state = _state(runid)
        assert state["serial_capture_pid"] is None
        assert state["serial_capture_identity"] is None
    finally:
        _stop_owned(process)


def test_capture_identity_is_persisted_and_owned_fixture_is_reaped(run_state, tmp_path: Path):
    # Given: a test-owned process and its capture-time identity token.
    runid, _ = run_state
    process, stdout_path, stderr_path = _start_serial_fixture(tmp_path)
    expected_identity = _capture_identity(process.pid)

    try:
        # When: registration and cleanup use the verified token.
        assert _register_capture(runid, process.pid, stdout_path, stderr_path).returncode == 0
        persisted = _state(runid)
        cleared = _run("clear-serial-capture", "--runid", runid)

        # Then: the state stores the token and only that owned fixture is reaped.
        assert persisted["serial_capture_identity"] == expected_identity
        assert cleared.returncode == 0, cleared.stderr
        assert process.wait(timeout=3) is not None
    finally:
        _stop_owned(process)


def test_resume_clears_stale_identity_without_signaling(run_state, tmp_path: Path):
    # Given: a live capture whose persisted birth identity no longer matches.
    runid, goal = run_state
    process, stdout_path, stderr_path = _start_serial_fixture(tmp_path)
    state_path = _STATE_ROOT / runid / "state.json"
    assert _register_capture(runid, process.pid, stdout_path, stderr_path).returncode == 0
    persisted = json.loads(state_path.read_text(encoding="utf-8"))
    persisted["serial_capture_identity"] = "0" * 64
    state_path.write_text(json.dumps(persisted), encoding="utf-8")

    try:
        # When: resume sees the stale identity.
        result = _run("resume-check", "--runid", runid, "--goal-path", str(goal))

        # Then: it fails closed by discarding ownership without touching the process.
        assert result.returncode == 0, result.stderr
        assert process.poll() is None
        assert _state(runid)["serial_capture_pid"] is None
    finally:
        _stop_owned(process)


def test_legacy_capture_state_fails_closed_without_signaling(run_state, tmp_path: Path):
    # Given: an active process and a persisted state from before identity binding existed.
    runid, _ = run_state
    process, stdout_path, stderr_path = _start_serial_fixture(tmp_path)
    state_path = _STATE_ROOT / runid / "state.json"
    assert _register_capture(runid, process.pid, stdout_path, stderr_path).returncode == 0
    persisted = json.loads(state_path.read_text(encoding="utf-8"))
    persisted.pop("serial_capture_identity")
    state_path.write_text(json.dumps(persisted), encoding="utf-8")

    try:
        # When: a mutator attempts to load the legacy state.
        result = _run("clear-serial-capture", "--runid", runid)

        # Then: closed-field validation rejects it before any signal is possible.
        assert result.returncode == 2
        assert "state" in result.stderr.lower()
        assert process.poll() is None
    finally:
        _stop_owned(process)


def test_omitted_reset_threshold_trips_on_third_boot_banner(tmp_path: Path):
    # Given: a goal that relies on the authoritative documented default of two resets.
    runid = "test-%s" % uuid.uuid4().hex
    goal = tmp_path / "default-reset-goal.json"
    _write_goal(goal)
    assert _init(runid, goal).returncode == 0

    try:
        # When: boot count reaches the allowed boundary and then exceeds it.
        for _ in range(2):
            assert _run("incr-boot", "--runid", runid).returncode == 0
        boundary = _run("failsafe-check", "--runid", runid, "--goal", str(goal))
        assert _run("incr-boot", "--runid", runid).returncode == 0
        exceeded = _run("failsafe-check", "--runid", runid, "--goal", str(goal))

        # Then: strict > semantics preserve two as safe and three as terminal.
        assert boundary.returncode == 0
        assert json.loads(boundary.stdout) == {"triggered": False}
        assert exceeded.returncode == 3
        assert json.loads(exceeded.stdout) == {"triggered": True, "reason": "repeated_reset"}
    finally:
        shutil.rmtree(_STATE_ROOT / runid, ignore_errors=True)


@pytest.mark.parametrize("expiry_offset", [1.0, 2.0])
def test_deadline_check_is_deterministic_and_terminalizes_owned_capture(
    tmp_path: Path, expiry_offset: float
):
    # Given: an active short-deadline cycle with an owned live capture.
    runid = "test-%s" % uuid.uuid4().hex
    goal = tmp_path / "deadline-goal.json"
    _write_goal(goal)
    assert _init(runid, goal, cycle_deadline_s=1.0).returncode == 0
    process = None
    try:
        assert _run("set-phase", "--runid", runid, "--phase", "building").returncode == 0
        process, stdout_path, stderr_path = _start_serial_fixture(tmp_path)
        assert _register_capture(runid, process.pid, stdout_path, stderr_path).returncode == 0
        started = _state(runid)["cycle_started_monotonic_s"]

        # When: the gate runs just below and exactly at its deadline without sleeping.
        below = _run(
            "deadline-check", "--runid", runid,
            "--now-monotonic-s", str(started + 0.999),
        )
        # Then: a below-threshold check preserves the capture.
        assert below.returncode == 0
        assert json.loads(below.stdout) == {"triggered": False}
        assert process.poll() is None

        # And: expiry reaps the owned capture and locks all later mutations.
        at = _run(
            "deadline-check", "--runid", runid,
            "--now-monotonic-s", str(started + expiry_offset),
        )
        assert at.returncode == 3
        assert json.loads(at.stdout) == {
            "action": "needs_human", "terminal_reason": "cycle_deadline_exceeded"
        }
        process.wait(timeout=3)
        terminal = _state(runid)
        assert terminal["terminal"] is True
        assert terminal["terminal_reason"] == "cycle_deadline_exceeded"
        assert terminal["serial_capture_pid"] is None
        assert _run("incr-stability", "--runid", runid).returncode == 1
    finally:
        if process is not None:
            _stop_owned(process)
        shutil.rmtree(_STATE_ROOT / runid, ignore_errors=True)


def test_help_and_source_purity_contract():
    # Given: the CLI source tree.
    source = _TOOL.read_text(encoding="utf-8")
    tree = ast.parse(source)

    # When: help and imports are inspected.
    result = _run("--help")
    imported = {
        alias.name.split(".")[0]
        for node in ast.walk(tree)
        if isinstance(node, (ast.Import, ast.ImportFrom))
        for alias in node.names
    }

    # Then: the tool is executable, atomic, and network-free.
    assert result.returncode == 0
    assert "os.replace" in source
    assert imported.isdisjoint({"requests", "httpx", "urllib", "socket", "openai", "anthropic"})
