from __future__ import annotations

import hashlib
import json
from pathlib import Path

from live_loop_e2e_support import (
    FIXTURE,
    cleanup,
    create_run,
    observation,
    output_json,
    require_ok,
    run,
    state,
    to_evaluated,
    verify_temporal,
    write_cycle,
)


def _decision(goal_id: str, iteration: int, action: str, converged: bool) -> dict:
    return {
        "iteration": iteration,
        "goal_id": goal_id,
        "goal_predicate_result": True,
        "converged": converged,
        "window_ok": True,
        "firmware_parameters_changed": True,
        "parameter_change_basis": {"serial_evidence": []},
        "serial_basis": {"anomaly_lines": [], "predicate_result": True},
        "aux_events": [],
        "aux_role": "classify",
        "action": action,
        "reason": "structured fixture decision",
    }


def _goal_decision(goal: Path, observation_path: Path, serial: Path, runid: str) -> dict:
    result = run(
        "goal",
        "--goal",
        str(goal),
        "--temporal-observation",
        str(observation_path),
        "--serial",
        str(serial),
        "--runid",
        runid,
    )
    return output_json(result)


def temporal_sessions_and_history(tmp_path: Path) -> None:
    first = tmp_path / "empty-first.json"
    second = tmp_path / "empty-second.json"
    first.write_text(json.dumps(observation(FIXTURE, (1, 2, 3))), encoding="utf-8")
    second.write_text(json.dumps(observation(FIXTURE, (4, 5, 6))), encoding="utf-8")
    for payload in (first, second):
        result = verify_temporal(FIXTURE, payload)
        assert result.returncode == 0, result.stderr
        assert result.stdout.strip() == "PASS"

    history = tmp_path / "history"
    prior = history / "cycle-with-detected-event"
    prior_observation = observation(FIXTURE, (10, 11, 12), confirmations=1)
    write_cycle(prior, prior_observation)
    current = tmp_path / "current"
    current_observation = observation(FIXTURE, (20, 21, 22), confirmations=2)
    current_path = write_cycle(current, current_observation)

    missing_history = verify_temporal(current, current_path)
    valid_history = verify_temporal(current, current_path, history=history)

    assert missing_history.returncode == 1
    assert "confirmations" in missing_history.stderr.lower()
    assert valid_history.returncode == 0, valid_history.stderr


def evidence_d3_and_two_cycle_success(tmp_path: Path) -> None:
    runid, goal, state_dir = create_run(tmp_path, "success")
    processes = []
    try:
        serial = tmp_path / "serial.log"
        serial.write_text("[ERR][fixture] transient\n[OK][fixture] mode=ready\n", encoding="utf-8")
        anomaly = run("scan", "--input", str(serial))
        assert anomaly.returncode == 3
        anomaly_lines = [json.loads(line)["line"] for line in anomaly.stdout.splitlines()]
        assert anomaly_lines == ["[ERR][fixture] transient"]

        history = state_dir / "history"
        first_cycle = history / "cycle-one"
        first_observation = observation(FIXTURE, (100, 101, 102))
        first_path = write_cycle(first_cycle, first_observation)
        cycle_one = state_dir / "cycle-1.json"
        cycle_one.write_text(json.dumps(first_observation), encoding="utf-8")
        first_temporal = verify_temporal(
            first_cycle,
            first_path,
            evidence=first_cycle / "temporal-audit.jsonl",
        )
        assert first_temporal.returncode == 0, first_temporal.stderr
        assert verify_temporal(FIXTURE, cycle_one).returncode == 0

        first_process = to_evaluated(runid, tmp_path, processes, from_idle=True)
        first_goal = _goal_decision(goal, first_path, serial, runid)
        assert first_goal["goal_id"] == hashlib.sha256(goal.read_bytes()).hexdigest()
        assert first_goal["goal_predicate_result"] is True
        assert first_goal["converged"] is False
        require_ok(run("state", "incr-stability", "--runid", runid))
        assert state(runid)["stability_count"] == 1
        require_ok(run("state", "set-phase", "--runid", runid, "--phase", "decided"))
        candidate = output_json(
            run(
                "state", "next-action", "--runid", runid, "--goal", str(goal),
                "--converged", "false", "--window-ok", "true",
                "--camera-index-now", "1", "--camera-name-now", "fixture-camera",
            )
        )
        assert candidate == {"action": "candidate_success"}

        first_record = _decision(first_goal["goal_id"], 0, candidate["action"], False)
        first_record["serial_basis"]["anomaly_lines"] = anomaly_lines
        invalid_path = state_dir / "decision-1-invalid.json"
        invalid_path.write_text(json.dumps(first_record), encoding="utf-8")
        invalid = run("decision", "--decision", str(invalid_path))
        assert invalid.returncode == 1
        assert "serial-basis" in invalid.stderr.lower()

        serial_line = serial.read_text(encoding="utf-8").splitlines()[1]
        first_record["parameter_change_basis"] = {
            "serial_evidence": [
                {
                    "capture_path": str(serial),
                    "line_number": 2,
                    "line_sha256": hashlib.sha256(serial_line.encode()).hexdigest(),
                }
            ]
        }
        decision_one = state_dir / "decision-1.json"
        decision_one.write_text(json.dumps(first_record), encoding="utf-8")
        require_ok(run("decision", "--decision", str(decision_one), "--evidence", str(state_dir / "decision-1-audit.txt")))
        require_ok(run("state", "advance", "--runid", runid, "--action", candidate["action"]))
        assert first_process.wait(timeout=3) is not None

        second_cycle = state_dir / "cycle-two-data"
        second_observation = observation(FIXTURE, (200, 201, 202))
        second_path = write_cycle(second_cycle, second_observation)
        cycle_two = state_dir / "cycle-2.json"
        cycle_two.write_text(json.dumps(second_observation), encoding="utf-8")
        second_temporal = verify_temporal(second_cycle, second_path, history=history)
        assert second_temporal.returncode == 0, second_temporal.stderr
        assert verify_temporal(FIXTURE, cycle_two).returncode == 0

        second_process = to_evaluated(runid, tmp_path, processes, from_idle=False)
        second_goal = _goal_decision(goal, second_path, serial, runid)
        assert second_goal["goal_predicate_result"] is True
        assert second_goal["converged"] is True
        require_ok(run("state", "incr-stability", "--runid", runid))
        assert state(runid)["stability_count"] == 2
        require_ok(run("state", "set-phase", "--runid", runid, "--phase", "decided"))
        success = output_json(
            run(
                "state", "next-action", "--runid", runid, "--goal", str(goal),
                "--converged", "true", "--window-ok", "true",
                "--camera-index-now", "1", "--camera-name-now", "fixture-camera",
            )
        )
        assert success == {"action": "success"}
        second_record = _decision(second_goal["goal_id"], 1, success["action"], True)
        second_record["parameter_change_basis"] = first_record["parameter_change_basis"]
        decision_two = state_dir / "decision-2.json"
        decision_two.write_text(json.dumps(second_record), encoding="utf-8")
        require_ok(run("decision", "--decision", str(decision_two)))
        require_ok(run("state", "advance", "--runid", runid, "--action", success["action"]))
        assert second_process.wait(timeout=3) is not None

        final_state = state(runid)
        assert state_dir.is_dir()
        assert final_state["terminal"] is True
        assert final_state["last_action"] == "success"
        assert final_state["stability_count"] == 2
    finally:
        cleanup([runid], processes)

