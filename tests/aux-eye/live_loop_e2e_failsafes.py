from __future__ import annotations

import json
import re
from pathlib import Path

from live_loop_e2e_cases import _decision
from live_loop_e2e_support import (
    MARKER,
    cleanup,
    capture_identity,
    create_run,
    output_json,
    require_ok,
    run,
    start_capture,
    state,
    to_evaluated,
)


def _needs_human(runid: str, goal: Path) -> dict:
    result = run(
        "state", "next-action", "--runid", runid, "--goal", str(goal),
        "--converged", "true", "--window-ok", "true",
    )
    assert result.returncode == 3
    payload = json.loads(result.stdout)
    require_ok(
        run(
            "state", "advance", "--runid", runid, "--action", "needs_human",
            "--terminal-reason", payload["terminal_reason"],
        )
    )
    assert state(runid)["terminal_reason"] == payload["terminal_reason"]
    return payload


def failsafe_causality(tmp_path: Path) -> None:
    runids = []
    processes = []
    try:
        runid, goal, _ = create_run(tmp_path, "silence", max_iterations=1)
        runids.append(runid)
        require_ok(run("state", "add-silence", "--runid", runid, "--seconds", "2"))
        assert output_json(run("state", "failsafe-check", "--runid", runid, "--goal", str(goal))) == {"triggered": False}
        process = to_evaluated(runid, tmp_path, processes, from_idle=True)
        require_ok(run("state", "set-phase", "--runid", runid, "--phase", "decided"))
        assert output_json(run("state", "next-action", "--runid", runid, "--goal", str(goal), "--converged", "false", "--window-ok", "false")) == {"action": "continue"}
        require_ok(run("state", "advance", "--runid", runid, "--action", "continue"))
        assert process.wait(timeout=3) is not None
        exhausted = _needs_human(runid, goal)
        assert exhausted["terminal_reason"] == "iteration_exhausted"
        assert run("state", "is-terminal", "--runid", runid).returncode == 3

        for label, increment, reason in (
            ("silence", "add-silence", "serial_silence"),
            ("visibility", "incr-visibility-loss", "visibility_loss"),
        ):
            runid, goal, _ = create_run(tmp_path, label)
            runids.append(runid)
            for _ in range(3 if increment == "add-silence" else 2):
                arguments = ("--seconds", "1") if increment == "add-silence" else ()
                require_ok(run("state", increment, "--runid", runid, *arguments))
            triggered = run("state", "failsafe-check", "--runid", runid, "--goal", str(goal))
            assert triggered.returncode == 3
            assert json.loads(triggered.stdout)["reason"] == reason
            assert _needs_human(runid, goal)["terminal_reason"] == reason

        runid, goal, _ = create_run(tmp_path, "boot")
        runids.append(runid)
        boot_capture = tmp_path / "boot.log"
        boot_capture.write_text("[BOOT] one\n[BOOT] two\n", encoding="utf-8")
        boot_scan = run("scan", "--input", str(boot_capture))
        assert boot_scan.returncode == 0
        assert boot_scan.stdout == ""
        for _ in re.findall(r"^\[BOOT\]", boot_capture.read_text(encoding="utf-8"), re.MULTILINE):
            require_ok(run("state", "incr-boot", "--runid", runid))
        boot = run("state", "failsafe-check", "--runid", runid, "--goal", str(goal))
        assert boot.returncode == 3
        assert json.loads(boot.stdout)["reason"] == "repeated_reset"
        assert _needs_human(runid, goal)["terminal_reason"] == "repeated_reset"

        runid, goal, state_dir = create_run(tmp_path, "camera")
        runids.append(runid)
        drift = run("state", "next-action", "--runid", runid, "--goal", str(goal), "--converged", "false", "--window-ok", "false", "--camera-name-now", "other-camera")
        assert drift.returncode == 3
        assert json.loads(drift.stdout)["terminal_reason"] == "camera_identity"
        require_ok(run("state", "advance", "--runid", runid, "--action", "needs_human"))
        conflict = _decision(state(runid)["goal_id"], 0, "needs_human", False)
        conflict["firmware_parameters_changed"] = False
        conflict.pop("parameter_change_basis")
        conflict["aux_role"] = "veto"
        conflict["aux_events"] = [{"kind": "oscillation", "status": "detected"}]
        conflict_path = state_dir / "decision-conflict.json"
        conflict_path.write_text(json.dumps(conflict), encoding="utf-8")
        require_ok(run("decision", "--decision", str(conflict_path)))
    finally:
        cleanup(runids, processes)


def serial_interruption_and_success_bypass(tmp_path: Path) -> None:
    runids = []
    processes = []
    try:
        runid, _, _ = create_run(tmp_path, "bypass")
        runids.append(runid)
        to_evaluated(runid, tmp_path, processes, from_idle=True)
        require_ok(run("state", "set-phase", "--runid", runid, "--phase", "decided"))
        direct_success = run("state", "advance", "--runid", runid, "--action", "success")
        assert direct_success.returncode != 0
        assert "not-converged" in direct_success.stderr

        runid, goal, _ = create_run(tmp_path, "flash")
        runids.append(runid)
        require_ok(run("state", "set-phase", "--runid", runid, "--phase", "building"))
        require_ok(run("state", "set-phase", "--runid", runid, "--phase", "serial_started"))
        missing_marker, stdout_path, stderr_path = start_capture(tmp_path, marker=False)
        processes.append(missing_marker)
        require_ok(run("state", "set-serial-capture", "--runid", runid, "--pid", str(missing_marker.pid), "--identity", capture_identity(missing_marker.pid), "--stdout", str(stdout_path), "--stderr", str(stderr_path)))
        assert run("state", "set-serial-ready", "--runid", runid).returncode == 1
        assert state(runid)["serial_capture_ready"] is False
        require_ok(run("state", "clear-serial-capture", "--runid", runid))
        assert missing_marker.wait(timeout=3) is not None

        ready, stdout_path, stderr_path = start_capture(tmp_path, marker=True)
        processes.append(ready)
        require_ok(run("state", "set-serial-capture", "--runid", runid, "--pid", str(ready.pid), "--identity", capture_identity(ready.pid), "--stdout", str(stdout_path), "--stderr", str(stderr_path)))
        require_ok(run("state", "set-serial-ready", "--runid", runid))
        assert ready.poll() is None
        assert stdout_path.read_text(encoding="utf-8") == ""
        assert stderr_path.read_text(encoding="utf-8") == MARKER
        require_ok(run("state", "set-phase", "--runid", runid, "--phase", "flash_started"))
        interrupted = run("state", "resume-check", "--runid", runid, "--goal-path", str(goal))
        assert interrupted.returncode == 3
        assert json.loads(interrupted.stdout) == {"action": "needs_human", "terminal_reason": "flash_interrupted"}
        assert ready.wait(timeout=3) is not None
    finally:
        cleanup(runids, processes)


def deadline_causality(tmp_path: Path) -> None:
    runids = []
    processes = []
    try:
        runid, _, _ = create_run(tmp_path, "deadline")
        runids.append(runid)
        require_ok(run("state", "set-phase", "--runid", runid, "--phase", "building"))
        process, stdout_path, stderr_path = start_capture(tmp_path, marker=True)
        processes.append(process)
        require_ok(
            run(
                "state", "set-serial-capture", "--runid", runid,
                "--pid", str(process.pid), "--identity", capture_identity(process.pid),
                "--stdout", str(stdout_path), "--stderr", str(stderr_path),
            )
        )
        started = state(runid)["cycle_started_monotonic_s"]

        below = run(
            "state", "deadline-check", "--runid", runid,
            "--now-monotonic-s", str(started + 179.999),
        )
        assert output_json(below) == {"triggered": False}
        assert process.poll() is None

        expired = run(
            "state", "deadline-check", "--runid", runid,
            "--now-monotonic-s", str(started + 180.0),
        )
        assert expired.returncode == 3
        assert json.loads(expired.stdout) == {
            "action": "needs_human", "terminal_reason": "cycle_deadline_exceeded"
        }
        assert process.wait(timeout=3) is not None
        terminal = state(runid)
        assert terminal["terminal"] is True
        assert terminal["last_action"] == "needs_human"
        assert terminal["terminal_reason"] == "cycle_deadline_exceeded"
        assert terminal["serial_capture_pid"] is None
        assert run("state", "reset-silence", "--runid", runid).returncode == 1
    finally:
        cleanup(runids, processes)
