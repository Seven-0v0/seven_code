from __future__ import annotations

import hashlib
import json
import shutil
import subprocess
import sys
import uuid
from pathlib import Path

import pytest


ROOT = Path(__file__).resolve().parents[2]
STATE_TOOL = ROOT / "tools" / "aux-eye" / "aux_eye_run_state.py"
STATE_ROOT = ROOT / ".omo" / "evidence" / "aux-eye-monitor"


def _run(*arguments: str) -> subprocess.CompletedProcess[str]:
    return subprocess.run(
        [sys.executable, str(STATE_TOOL), *arguments],
        capture_output=True,
        text=True,
        cwd=ROOT,
        check=False,
    )


def _state(runid: str) -> dict:
    result = _run("get", "--runid", runid)
    assert result.returncode == 0, result.stderr
    return json.loads(result.stdout)


def _capture_identity(pid: int) -> str:
    result = _run("capture-identity", "--pid", str(pid))
    assert result.returncode == 0, result.stderr
    return json.loads(result.stdout)["identity"]


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
def monitor_run(tmp_path: Path):
    runid = "integration-%s" % uuid.uuid4().hex
    goal = tmp_path / "goal.json"
    goal.write_text(
        json.dumps(
            {
                "goal_description": "integration fixture",
                "decision": {"kind": "agent_judgment", "stability_windows": 2},
            }
        ),
        encoding="utf-8",
    )
    initialized = _run(
        "init", "--runid", runid,
        "--goal-id", hashlib.sha256(goal.read_bytes()).hexdigest(),
        "--goal-path", str(goal), "--camera-index", "1",
        "--camera-name", "integration-camera", "--serial-device", "/dev/integration",
        "--serial-baud", "115200", "--max-iterations", "3",
    )
    assert initialized.returncode == 0, initialized.stderr

    yield runid, goal

    _run("clear-serial-capture", "--runid", runid)
    shutil.rmtree(STATE_ROOT / runid, ignore_errors=True)


def _advance_to_evaluated(runid: str, tmp_path: Path, start_from_idle: bool) -> subprocess.Popen:
    if start_from_idle:
        building = _run("set-phase", "--runid", runid, "--phase", "building")
        assert building.returncode == 0, building.stderr
    serial_started = _run("set-phase", "--runid", runid, "--phase", "serial_started")
    assert serial_started.returncode == 0, serial_started.stderr

    stdout_path = tmp_path / (uuid.uuid4().hex + ".out")
    stderr_path = tmp_path / (uuid.uuid4().hex + ".err")
    stdout_path.write_text("", encoding="utf-8")
    stderr_path.write_text("[OK][serial] capturing from integration\n", encoding="utf-8")
    process = subprocess.Popen([sys.executable, "-c", "import time; time.sleep(60)"])
    registered = _run(
        "set-serial-capture", "--runid", runid, "--pid", str(process.pid),
        "--identity", _capture_identity(process.pid),
        "--stdout", str(stdout_path), "--stderr", str(stderr_path),
    )
    assert registered.returncode == 0, registered.stderr
    ready = _run("set-serial-ready", "--runid", runid)
    assert ready.returncode == 0, ready.stderr
    flash_started = _run("set-phase", "--runid", runid, "--phase", "flash_started")
    assert flash_started.returncode == 0, flash_started.stderr
    flash_done = _run("set-flash-done", "--runid", runid)
    assert flash_done.returncode == 0, flash_done.stderr
    for phase in ("flashed", "captured", "evaluated"):
        advanced = _run("set-phase", "--runid", runid, "--phase", phase)
        assert advanced.returncode == 0, advanced.stderr
    return process


def _decide(runid: str, goal: Path, converged: str, window_ok: str) -> dict:
    decided = _run("set-phase", "--runid", runid, "--phase", "decided")
    assert decided.returncode == 0, decided.stderr
    next_action = _run(
        "next-action", "--runid", runid, "--goal", str(goal),
        "--converged", converged, "--window-ok", window_ok,
        "--camera-index-now", "1", "--camera-name-now", "integration-camera",
    )
    assert next_action.returncode == 0, next_action.stderr
    return json.loads(next_action.stdout)


def test_documented_lifecycle_reaches_continue_and_advances(monitor_run, tmp_path: Path):
    # Given: an initialized run and a live serial capture with the stderr readiness marker.
    runid, goal = monitor_run
    process = _advance_to_evaluated(runid, tmp_path, start_from_idle=True)

    try:
        # When: the documented evaluated -> decided -> next-action -> advance sequence selects continue.
        assert _state(runid)["cycle_phase"] == "evaluated"
        action = _decide(runid, goal, converged="false", window_ok="false")
        advanced = _run("advance", "--runid", runid, "--action", action["action"])

        # Then: the action is persisted, consumed, and starts the next build cycle.
        assert action == {"action": "continue"}
        assert advanced.returncode == 0, advanced.stderr
        assert _state(runid)["cycle_phase"] == "building"
        assert process.wait(timeout=3) is not None
    finally:
        _stop_owned(process)


def test_documented_lifecycle_reaches_candidate_success_then_success(monitor_run, tmp_path: Path):
    # Given: the first of two qualifying cycles with an owned live serial capture.
    runid, goal = monitor_run
    first = _advance_to_evaluated(runid, tmp_path, start_from_idle=True)

    try:
        # When: the first cycle selects candidate_success and the second selects success.
        first_window = _run("incr-stability", "--runid", runid)
        assert first_window.returncode == 0, first_window.stderr
        candidate = _decide(runid, goal, converged="false", window_ok="true")
        first_advance = _run("advance", "--runid", runid, "--action", candidate["action"])
        assert candidate == {"action": "candidate_success"}
        assert first_advance.returncode == 0, first_advance.stderr
        assert first.wait(timeout=3) is not None

        second = _advance_to_evaluated(runid, tmp_path, start_from_idle=False)
        try:
            second_window = _run("incr-stability", "--runid", runid)
            assert second_window.returncode == 0, second_window.stderr
            success = _decide(runid, goal, converged="true", window_ok="true")
            second_advance = _run("advance", "--runid", runid, "--action", success["action"])

            # Then: the candidate action remains nonterminal and the second bound action terminalizes success.
            assert success == {"action": "success"}
            assert second_advance.returncode == 0, second_advance.stderr
            state = _state(runid)
            assert state["terminal"] is True
            assert state["last_action"] == "success"
            assert state["stability_count"] == 2
            assert second.wait(timeout=3) is not None
        finally:
            _stop_owned(second)
    finally:
        _stop_owned(first)
