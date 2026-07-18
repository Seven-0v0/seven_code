from __future__ import annotations

import hashlib
import json
import shutil
import subprocess
import sys
import uuid
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
FIXTURE = ROOT / "tests" / "fixtures" / "aux-eye" / "temporal-still"
STATE_ROOT = ROOT / ".omo" / "evidence" / "aux-eye-monitor"
TOOLS = {
    "decision": ROOT / "tools" / "aux-eye" / "verify_aux_eye_decision.py",
    "goal": ROOT / "tools" / "aux-eye" / "aux_eye_goal_decide.py",
    "scan": ROOT / "tools" / "aux-eye" / "serial_anomaly_scan.py",
    "state": ROOT / "tools" / "aux-eye" / "aux_eye_run_state.py",
    "temporal": ROOT / "tools" / "aux-eye" / "verify_aux_eye_temporal.py",
}
MARKER = "[OK][serial] capturing from fixture\n"


def run(tool: str, *arguments: str, input_text: str | None = None) -> subprocess.CompletedProcess[str]:
    return subprocess.run(
        [sys.executable, str(TOOLS[tool]), *arguments],
        input=input_text,
        capture_output=True,
        text=True,
        cwd=ROOT,
        check=False,
    )


def require_ok(result: subprocess.CompletedProcess[str]) -> None:
    assert result.returncode == 0, result.stderr


def output_json(result: subprocess.CompletedProcess[str]) -> dict:
    require_ok(result)
    return json.loads(result.stdout)


def capture_identity(pid: int) -> str:
    return output_json(run("state", "capture-identity", "--pid", str(pid)))["identity"]


def observation(
    sequence_dir: Path,
    timestamps: tuple[int, int, int],
    confirmations: int | None = None,
) -> dict:
    frames = []
    for index, timestamp in enumerate(timestamps):
        relative = Path("frames") / (str(index) + ".jpg")
        frames.append(
            {
                "path": relative.as_posix(),
                "sha256": hashlib.sha256((sequence_dir / relative).read_bytes()).hexdigest(),
                "ts": timestamp,
            }
        )
    payload = {"frames": frames, "temporal_events": [], "visible": False}
    if confirmations is not None:
        payload["temporal_events"] = [
            {
                "kind": "oscillation",
                "status": "detected",
                "start_frame": 0,
                "end_frame": 2,
                "confirmations": confirmations,
            }
        ]
    return payload


def write_cycle(cycle_dir: Path, payload: dict) -> Path:
    shutil.copytree(FIXTURE / "frames", cycle_dir / "frames")
    path = cycle_dir / "observation.json"
    path.write_text(json.dumps(payload), encoding="utf-8")
    return path


def verify_temporal(
    sequence_dir: Path,
    observation_path: Path,
    history: Path | None = None,
    evidence: Path | None = None,
) -> subprocess.CompletedProcess[str]:
    arguments = [
        "--sequence-dir",
        str(sequence_dir),
        "--observation",
        str(observation_path),
    ]
    if history is not None:
        arguments.extend(("--history", str(history)))
    if evidence is not None:
        arguments.extend(("--evidence", str(evidence)))
    return run("temporal", *arguments)


def create_run(
    tmp_path: Path,
    label: str,
    *,
    max_iterations: int = 3,
    serial_silence_s: int = 3,
    max_consecutive_resets: int = 1,
    visibility_loss_windows: int = 2,
) -> tuple[str, Path, Path]:
    goal = tmp_path / (label + "-goal.json")
    goal.write_text(
        json.dumps(
            {
                "goal_description": "deterministic fixture goal",
                "decision": {
                    "kind": "predicate",
                    "predicate": 'serial.mode == "ready"',
                    "stability_windows": 2,
                    "serial_silence_s": serial_silence_s,
                    "max_consecutive_resets": max_consecutive_resets,
                    "visibility_loss_windows": visibility_loss_windows,
                },
            }
        ),
        encoding="utf-8",
    )
    runid = "e2e-" + label + "-" + uuid.uuid4().hex
    initialized = run(
        "state",
        "init",
        "--runid",
        runid,
        "--goal-id",
        hashlib.sha256(goal.read_bytes()).hexdigest(),
        "--goal-path",
        str(goal),
        "--camera-index",
        "1",
        "--camera-name",
        "fixture-camera",
        "--serial-device",
        "/dev/fixture",
        "--serial-baud",
        "115200",
        "--max-iterations",
        str(max_iterations),
    )
    require_ok(initialized)
    return runid, goal, STATE_ROOT / runid


def state(runid: str) -> dict:
    return output_json(run("state", "get", "--runid", runid))


def start_capture(tmp_path: Path, marker: bool) -> tuple[subprocess.Popen, Path, Path]:
    stdout_path = tmp_path / (uuid.uuid4().hex + ".out")
    stderr_path = tmp_path / (uuid.uuid4().hex + ".err")
    stdout_path.write_text("", encoding="utf-8")
    stderr_path.write_text(MARKER if marker else "", encoding="utf-8")
    process = subprocess.Popen([sys.executable, "-c", "import time; time.sleep(60)"])
    return process, stdout_path, stderr_path


def stop_capture(process: subprocess.Popen) -> None:
    if process.poll() is not None:
        return
    process.terminate()
    try:
        process.wait(timeout=3)
    except subprocess.TimeoutExpired:
        process.kill()
        process.wait(timeout=3)


def to_evaluated(
    runid: str,
    tmp_path: Path,
    processes: list[subprocess.Popen],
    from_idle: bool,
) -> subprocess.Popen:
    if from_idle:
        require_ok(run("state", "set-phase", "--runid", runid, "--phase", "building"))
    require_ok(run("state", "set-phase", "--runid", runid, "--phase", "serial_started"))
    process, stdout_path, stderr_path = start_capture(tmp_path, marker=True)
    processes.append(process)
    require_ok(
        run(
            "state",
            "set-serial-capture",
            "--runid",
            runid,
            "--pid",
            str(process.pid),
            "--identity",
            capture_identity(process.pid),
            "--stdout",
            str(stdout_path),
            "--stderr",
            str(stderr_path),
        )
    )
    require_ok(run("state", "set-serial-ready", "--runid", runid))
    require_ok(run("state", "set-phase", "--runid", runid, "--phase", "flash_started"))
    require_ok(run("state", "set-flash-done", "--runid", runid))
    for phase in ("flashed", "captured", "evaluated"):
        require_ok(run("state", "set-phase", "--runid", runid, "--phase", phase))
    return process


def cleanup(runids: list[str], processes: list[subprocess.Popen]) -> None:
    for runid in runids:
        state_dir = STATE_ROOT / runid
        if state_dir.is_dir():
            result = run("state", "clear-serial-capture", "--runid", runid)
            assert result.returncode in (0, 1), result.stderr
    for process in processes:
        stop_capture(process)
        assert process.poll() is not None
    for runid in runids:
        state_dir = STATE_ROOT / runid
        shutil.rmtree(state_dir, ignore_errors=True)
        assert not state_dir.exists()
