#!/usr/bin/env python3
# noqa: SIZE_OK - One authoritative CLI/state-machine contract from Todo 6.
from __future__ import annotations

import argparse
import hashlib
import json
import math
import os
import re
import signal
import subprocess
import sys
import tempfile
import time
from pathlib import Path
from typing import Dict, NamedTuple, Union


JsonScalar = Union[str, int, float, bool, None]
State = Dict[str, JsonScalar]

STATE_FIELDS = {
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
PHASES = (
    "idle", "building", "serial_started", "flash_started", "flashed",
    "captured", "evaluated", "decided",
)
PHASE_NEXT = dict(zip(PHASES, PHASES[1:]))
ACTIONS = ("continue", "candidate_success", "success", "safe_abort", "needs_human")
TERMINAL_ACTIONS = ("success", "safe_abort", "needs_human")
SERIAL_READY_MARKER = "[OK][serial] capturing from"
RUNID_PATTERN = re.compile(r"^[A-Za-z0-9][A-Za-z0-9._-]{0,127}$")
SHA256_PATTERN = re.compile(r"^[A-Fa-f0-9]{64}$")
DEFAULT_SERIAL_SILENCE_S = 30.0
DEFAULT_MAX_CONSECUTIVE_RESETS = 2
DEFAULT_VISIBILITY_LOSS_WINDOWS = 2
DEFAULT_CYCLE_DEADLINE_S = 180.0


class StateError(Exception):
    def __init__(self, message: str, exit_code: int = 2) -> None:
        super().__init__(message)
        self.exit_code = exit_code


class Thresholds(NamedTuple):
    serial_silence_s: float
    max_consecutive_resets: int
    visibility_loss_windows: int


class StateParser(argparse.ArgumentParser):
    def error(self, message: str) -> None:
        self.print_usage(sys.stderr)
        self.exit(2, "[ERR][state] %s\n" % message)


def _repo_root() -> Path:
    return Path(__file__).resolve().parents[2]


def _parse_runid(value: str) -> str:
    if ".." in value or not RUNID_PATTERN.fullmatch(value):
        raise argparse.ArgumentTypeError("unsafe runid: %s" % value)
    return value


def _parse_bool(value: str) -> bool:
    normalized = value.lower()
    if normalized == "true":
        return True
    if normalized == "false":
        return False
    raise argparse.ArgumentTypeError("expected true or false")


def _positive_int(value: str) -> int:
    parsed = int(value)
    if parsed <= 0:
        raise argparse.ArgumentTypeError("expected a positive integer")
    return parsed


def _nonnegative_float(value: str) -> float:
    parsed = float(value)
    if parsed < 0 or not math.isfinite(parsed):
        raise argparse.ArgumentTypeError("expected finite seconds >= 0")
    return parsed


def _positive_float(value: str) -> float:
    parsed = _nonnegative_float(value)
    if parsed <= 0:
        raise argparse.ArgumentTypeError("expected finite seconds > 0")
    return parsed


def _state_path(runid: str) -> Path:
    _parse_runid(runid)
    return _repo_root() / ".omo" / "evidence" / "aux-eye-monitor" / runid / "state.json"


def _required_string(state: State, field: str) -> str:
    value = state[field]
    if not isinstance(value, str) or not value:
        raise StateError("invalid state field %s" % field)
    return value


def _optional_string(state: State, field: str) -> str | None:
    value = state[field]
    if value is not None and not isinstance(value, str):
        raise StateError("invalid state field %s" % field)
    return value


def _required_int(state: State, field: str, minimum: int = 0) -> int:
    value = state[field]
    if type(value) is not int or value < minimum:
        raise StateError("invalid state field %s" % field)
    return value


def _optional_int(state: State, field: str) -> int | None:
    value = state[field]
    if value is not None and (type(value) is not int or value <= 0):
        raise StateError("invalid state field %s" % field)
    return value


def _required_number(state: State, field: str) -> float:
    value = state[field]
    if type(value) not in (int, float) or value < 0 or not math.isfinite(float(value)):
        raise StateError("invalid state field %s" % field)
    return float(value)


def _required_bool(state: State, field: str) -> bool:
    value = state[field]
    if type(value) is not bool:
        raise StateError("invalid state field %s" % field)
    return value


def _optional_bool(state: State, field: str) -> bool | None:
    value = state[field]
    if value is not None and type(value) is not bool:
        raise StateError("invalid state field %s" % field)
    return value


def _optional_number(state: State, field: str) -> float | None:
    value = state[field]
    if value is None:
        return None
    if type(value) not in (int, float) or not math.isfinite(float(value)):
        raise StateError("invalid state field %s" % field)
    return float(value)


def _validate_state(state: State) -> None:
    if set(state) != STATE_FIELDS:
        raise StateError("invalid state fields")
    runid = _required_string(state, "runid")
    try:
        _parse_runid(runid)
    except argparse.ArgumentTypeError as exc:
        raise StateError(str(exc)) from exc
    goal_id = _required_string(state, "goal_id")
    if not SHA256_PATTERN.fullmatch(goal_id):
        raise StateError("invalid state field goal_id")
    for field in ("goal_path", "camera_name", "serial_device"):
        _required_string(state, field)
    for field in (
        "iteration", "camera_index", "stability_count", "consecutive_boot_count",
        "visibility_loss_count",
    ):
        _required_int(state, field)
    _required_int(state, "serial_baud", 1)
    max_iterations = _required_int(state, "max_iterations", 1)
    if max_iterations > 20:
        raise StateError("invalid state field max_iterations")
    _required_number(state, "serial_silence_accum_s")
    cycle_deadline_s = _required_number(state, "cycle_deadline_s")
    if cycle_deadline_s <= 0:
        raise StateError("invalid state field cycle_deadline_s")
    cycle_started = _optional_number(state, "cycle_started_monotonic_s")
    created_ts = _required_number(state, "created_ts")
    updated_ts = _required_number(state, "updated_ts")
    if updated_ts < created_ts:
        raise StateError("updated_ts precedes created_ts")
    phase = _required_string(state, "cycle_phase")
    if phase not in PHASES:
        raise StateError("invalid state field cycle_phase")
    capture_pid = _optional_int(state, "serial_capture_pid")
    capture_identity = _optional_string(state, "serial_capture_identity")
    capture_out = _optional_string(state, "serial_capture_out")
    capture_err = _optional_string(state, "serial_capture_err")
    ready = _required_bool(state, "serial_capture_ready")
    if capture_pid is None and (
        capture_identity is not None or capture_out is not None or capture_err is not None or ready
    ):
        raise StateError("inconsistent serial capture state")
    if capture_pid is not None and (
        not capture_identity or not SHA256_PATTERN.fullmatch(capture_identity)
        or not capture_out or not capture_err
    ):
        raise StateError("incomplete serial capture paths")
    pending = _optional_string(state, "pending_action")
    pending_converged = _optional_bool(state, "pending_action_converged")
    pending_window_ok = _optional_bool(state, "pending_action_window_ok")
    if pending is not None and pending not in ACTIONS:
        raise StateError("invalid state field pending_action")
    if pending is None and (pending_converged is not None or pending_window_ok is not None):
        raise StateError("inconsistent pending action state")
    if pending is not None and (pending_converged is None or pending_window_ok is None):
        raise StateError("incomplete pending action binding")
    last_action = _optional_string(state, "last_action")
    if last_action is not None and last_action not in ACTIONS:
        raise StateError("invalid state field last_action")
    terminal = _required_bool(state, "terminal")
    terminal_reason = _optional_string(state, "terminal_reason")
    if terminal and last_action not in TERMINAL_ACTIONS:
        raise StateError("terminal state lacks terminal action")
    if not terminal and terminal_reason is not None:
        raise StateError("nonterminal state has terminal reason")
    flash_done = _required_bool(state, "build_flash_done")
    if flash_done and phase in ("idle", "building", "serial_started"):
        raise StateError("flash completion precedes flash phase")
    if not terminal and phase == "idle" and cycle_started is not None:
        raise StateError("idle state has active cycle deadline")
    if not terminal and phase != "idle" and cycle_started is None:
        raise StateError("active cycle lacks deadline start")


def _write_state(path: Path, state: State) -> None:
    _validate_state(state)
    temporary_path: Path | None = None
    try:
        with tempfile.NamedTemporaryFile(
            mode="w", encoding="utf-8", dir=path.parent,
            prefix=".state-", suffix=".tmp", delete=False,
        ) as handle:
            temporary_path = Path(handle.name)
            json.dump(state, handle, sort_keys=True, indent=2)
            handle.write("\n")
            handle.flush()
            os.fsync(handle.fileno())
        os.replace(str(temporary_path), str(path))
        directory_descriptor = os.open(str(path.parent), os.O_RDONLY)
        try:
            os.fsync(directory_descriptor)
        finally:
            os.close(directory_descriptor)
    finally:
        if temporary_path is not None and temporary_path.exists():
            temporary_path.unlink()


def _load_state(runid: str) -> tuple[Path, State]:
    path = _state_path(runid)
    try:
        raw = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, UnicodeDecodeError, json.JSONDecodeError) as exc:
        raise StateError("cannot load state: %s" % exc) from exc
    if not isinstance(raw, dict):
        raise StateError("state must be a JSON object")
    state: State = dict(raw)
    _validate_state(state)
    if state["runid"] != runid:
        raise StateError("state runid mismatch")
    return path, state


def _save_state(path: Path, state: State) -> None:
    next_state = dict(state)
    next_state["updated_ts"] = max(time.time(), _required_number(state, "created_ts"))
    _write_state(path, next_state)
    state.clear()
    state.update(next_state)


def _mutable_state(runid: str) -> tuple[Path, State]:
    path, state = _load_state(runid)
    if _required_bool(state, "terminal"):
        raise StateError("terminal run is mutation-locked", 1)
    return path, state


def _sha256(path: Path) -> str:
    digest = hashlib.sha256()
    try:
        with path.open("rb") as handle:
            for chunk in iter(lambda: handle.read(65536), b""):
                digest.update(chunk)
    except OSError as exc:
        raise StateError("cannot read goal: %s" % exc) from exc
    return digest.hexdigest()


def _goal_payload(path: Path) -> dict:
    try:
        payload = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, UnicodeDecodeError, json.JSONDecodeError) as exc:
        raise StateError("cannot load goal: %s" % exc) from exc
    if not isinstance(payload, dict) or not isinstance(payload.get("goal_description"), str):
        raise StateError("invalid goal document")
    decision = payload.get("decision")
    if not isinstance(decision, dict) or decision.get("kind") not in ("agent_judgment", "predicate"):
        raise StateError("invalid goal decision")
    return payload


def _goal_thresholds(path: Path, state: State) -> Thresholds:
    if _sha256(path) != _required_string(state, "goal_id").lower():
        raise StateError("goal identity mismatch", 1)
    payload = _goal_payload(path)
    decision = payload["decision"]
    silence = decision.get("serial_silence_s", DEFAULT_SERIAL_SILENCE_S)
    resets = decision.get("max_consecutive_resets", DEFAULT_MAX_CONSECUTIVE_RESETS)
    visibility = decision.get("visibility_loss_windows", DEFAULT_VISIBILITY_LOSS_WINDOWS)
    if type(silence) not in (int, float) or silence <= 0 or not math.isfinite(float(silence)):
        raise StateError("invalid goal serial_silence_s")
    if type(resets) is not int or resets < 0:
        raise StateError("invalid goal max_consecutive_resets")
    if type(visibility) is not int or visibility < 1:
        raise StateError("invalid goal visibility_loss_windows")
    return Thresholds(float(silence), resets, visibility)


def _process_identity(pid: int) -> str | None:
    try:
        result = subprocess.run(
            [
                "ps", "-p", str(pid), "-o", "lstart=", "-o", "uid=",
                "-o", "ppid=", "-o", "pgid=", "-o", "command=",
            ],
            capture_output=True,
            text=True,
            check=False,
        )
    except OSError:
        return None
    if result.returncode != 0 or not result.stdout.strip():
        return None
    material = "%d\0%s" % (pid, result.stdout.rstrip("\n"))
    return hashlib.sha256(material.encode("utf-8")).hexdigest()


def _capture_is_owned(state: State) -> bool:
    pid = _optional_int(state, "serial_capture_pid")
    identity = _optional_string(state, "serial_capture_identity")
    if pid is None or identity is None:
        return False
    current_identity = _process_identity(pid)
    return current_identity is not None and current_identity == identity


def _serial_is_ready(state: State) -> bool:
    stderr_path = _optional_string(state, "serial_capture_err")
    if not _capture_is_owned(state) or stderr_path is None:
        return False
    try:
        return SERIAL_READY_MARKER in Path(stderr_path).read_text(encoding="utf-8", errors="replace")
    except OSError:
        return False


def _terminate_capture(state: State) -> None:
    pid = _optional_int(state, "serial_capture_pid")
    if pid is None or not _capture_is_owned(state):
        return
    try:
        os.kill(pid, signal.SIGTERM)
        time.sleep(0.05)
        if _capture_is_owned(state):
            os.kill(pid, signal.SIGKILL)
    except (PermissionError, ProcessLookupError):
        return


def _clear_capture_fields(state: State) -> None:
    state["serial_capture_pid"] = None
    state["serial_capture_identity"] = None
    state["serial_capture_out"] = None
    state["serial_capture_err"] = None
    state["serial_capture_ready"] = False


def _failsafe_reason(state: State, thresholds: Thresholds) -> str | None:
    if _required_number(state, "serial_silence_accum_s") >= thresholds.serial_silence_s:
        return "serial_silence"
    if _required_int(state, "consecutive_boot_count") > thresholds.max_consecutive_resets:
        return "repeated_reset"
    if _required_int(state, "visibility_loss_count") >= thresholds.visibility_loss_windows:
        return "visibility_loss"
    return None


def _print_json(payload: dict) -> None:
    print(json.dumps(payload, sort_keys=True))


def _print_state(state: State) -> None:
    print(json.dumps(state, sort_keys=True))


def _ok(message: str) -> None:
    print("[OK][state] %s" % message, file=sys.stderr)


def _cmd_init(args: argparse.Namespace) -> int:
    goal_path = Path(args.goal_path)
    goal_id = _sha256(goal_path)
    _goal_payload(goal_path)
    if not SHA256_PATTERN.fullmatch(args.goal_id) or args.goal_id.lower() != goal_id:
        raise StateError("goal identity mismatch")
    path = _state_path(args.runid)
    if path.parent.exists():
        raise StateError("run already exists", 1)
    now = time.time()
    state: State = {
        "runid": args.runid, "iteration": 0, "cycle_phase": "idle",
        "goal_id": goal_id, "goal_path": args.goal_path,
        "camera_index": args.camera_index, "camera_name": args.camera_name,
        "serial_device": args.serial_device, "serial_baud": args.serial_baud,
        "max_iterations": args.max_iterations, "stability_count": 0,
        "consecutive_boot_count": 0, "visibility_loss_count": 0,
        "serial_silence_accum_s": 0.0, "serial_capture_pid": None,
        "serial_capture_identity": None, "serial_capture_out": None,
        "serial_capture_err": None,
        "serial_capture_ready": False, "pending_action": None,
        "pending_action_converged": None, "pending_action_window_ok": None,
        "build_flash_done": False, "last_action": None, "terminal": False,
        "terminal_reason": None, "cycle_deadline_s": args.cycle_deadline_s,
        "cycle_started_monotonic_s": None, "created_ts": now, "updated_ts": now,
    }
    _validate_state(state)
    path.parent.mkdir(parents=True, exist_ok=False)
    try:
        _write_state(path, state)
    except OSError:
        path.parent.rmdir()
        raise
    _print_state(state)
    _ok("initialized %s" % args.runid)
    return 0


def _cmd_get(args: argparse.Namespace) -> int:
    _, state = _load_state(args.runid)
    _print_state(state)
    return 0


def _cmd_set_phase(args: argparse.Namespace) -> int:
    path, state = _mutable_state(args.runid)
    current = _required_string(state, "cycle_phase")
    expected = PHASE_NEXT.get(current)
    if args.phase != expected:
        raise StateError("illegal phase transition %s -> %s" % (current, args.phase), 1)
    if args.phase == "flash_started":
        stored_ready = _required_bool(state, "serial_capture_ready")
        live_ready = _serial_is_ready(state)
        if not stored_ready or not live_ready:
            state["serial_capture_ready"] = False
            _save_state(path, state)
            raise StateError("serial capture is not ready before flash", 1)
    if args.phase == "flashed" and not _required_bool(state, "build_flash_done"):
        raise StateError("flash completion is not recorded", 1)
    state["cycle_phase"] = args.phase
    if args.phase == "building":
        state["cycle_started_monotonic_s"] = time.monotonic()
    _save_state(path, state)
    _print_state(state)
    _ok("phase=%s" % args.phase)
    return 0


def _change_counter(args: argparse.Namespace, field: str, reset: bool = False) -> int:
    path, state = _mutable_state(args.runid)
    state[field] = 0 if reset else _required_int(state, field) + 1
    _save_state(path, state)
    _print_state(state)
    return 0


def _cmd_incr_stability(args: argparse.Namespace) -> int:
    return _change_counter(args, "stability_count")


def _cmd_reset_stability(args: argparse.Namespace) -> int:
    return _change_counter(args, "stability_count", True)


def _cmd_incr_boot(args: argparse.Namespace) -> int:
    return _change_counter(args, "consecutive_boot_count")


def _cmd_reset_boot(args: argparse.Namespace) -> int:
    return _change_counter(args, "consecutive_boot_count", True)


def _cmd_incr_visibility(args: argparse.Namespace) -> int:
    return _change_counter(args, "visibility_loss_count")


def _cmd_reset_visibility(args: argparse.Namespace) -> int:
    return _change_counter(args, "visibility_loss_count", True)


def _cmd_add_silence(args: argparse.Namespace) -> int:
    path, state = _mutable_state(args.runid)
    state["serial_silence_accum_s"] = _required_number(state, "serial_silence_accum_s") + args.seconds
    _save_state(path, state)
    _print_state(state)
    return 0


def _cmd_reset_silence(args: argparse.Namespace) -> int:
    path, state = _mutable_state(args.runid)
    state["serial_silence_accum_s"] = 0.0
    _save_state(path, state)
    _print_state(state)
    return 0


def _cmd_set_serial_capture(args: argparse.Namespace) -> int:
    path, state = _mutable_state(args.runid)
    if Path(args.stdout).resolve() == Path(args.stderr).resolve():
        raise StateError("serial stdout and stderr must be distinct paths", 1)
    actual_identity = _process_identity(args.pid)
    if actual_identity is None or actual_identity != args.identity:
        raise StateError("serial capture identity could not be verified", 1)
    existing_pid = _optional_int(state, "serial_capture_pid")
    if existing_pid is not None and existing_pid != args.pid:
        _terminate_capture(state)
    state["serial_capture_pid"] = args.pid
    state["serial_capture_identity"] = actual_identity
    state["serial_capture_out"] = args.stdout
    state["serial_capture_err"] = args.stderr
    state["serial_capture_ready"] = False
    try:
        _save_state(path, state)
    except OSError:
        _terminate_capture(state)
        raise
    _print_state(state)
    return 0


def _cmd_set_serial_ready(args: argparse.Namespace) -> int:
    path, state = _mutable_state(args.runid)
    ready = _serial_is_ready(state)
    state["serial_capture_ready"] = ready
    _save_state(path, state)
    _print_state(state)
    if ready:
        _ok("serial capture ready")
        return 0
    print("[ERR][state] serial capture is not ready", file=sys.stderr)
    return 1


def _cmd_clear_serial_capture(args: argparse.Namespace) -> int:
    path, state = _mutable_state(args.runid)
    _terminate_capture(state)
    _clear_capture_fields(state)
    _save_state(path, state)
    _print_state(state)
    return 0


def _cmd_set_flash_done(args: argparse.Namespace) -> int:
    path, state = _mutable_state(args.runid)
    if _required_string(state, "cycle_phase") != "flash_started":
        raise StateError("flash completion requires flash_started phase", 1)
    stored_ready = _required_bool(state, "serial_capture_ready")
    live_ready = _serial_is_ready(state)
    if not stored_ready or not live_ready:
        state["serial_capture_ready"] = False
        _save_state(path, state)
        raise StateError("serial capture stopped during flash", 1)
    state["build_flash_done"] = True
    _save_state(path, state)
    _print_state(state)
    return 0


def _cmd_advance(args: argparse.Namespace) -> int:
    path, state = _mutable_state(args.runid)
    if args.action in ("continue", "candidate_success", "success"):
        if _required_string(state, "cycle_phase") != "decided":
            raise StateError("advance requires decided phase", 1)
    if args.action == "success":
        if state["pending_action"] != "success" or state["pending_action_converged"] is not True:
            raise StateError("not-converged: success lacks pending convergence binding", 1)
    if args.action == "candidate_success":
        if state["pending_action"] != "candidate_success" or state["pending_action_window_ok"] is not True:
            raise StateError("not-candidate: candidate_success lacks pending window binding", 1)
    pending_action = _optional_string(state, "pending_action")
    if pending_action is not None and args.action != pending_action:
        raise StateError(
            "pending-action-mismatch: expected %s, got %s"
            % (pending_action, args.action),
            4,
        )
    _terminate_capture(state)
    _clear_capture_fields(state)
    state["last_action"] = args.action
    state["pending_action"] = None
    state["pending_action_converged"] = None
    state["pending_action_window_ok"] = None
    if args.action in TERMINAL_ACTIONS:
        state["terminal"] = True
        state["terminal_reason"] = args.terminal_reason
        if args.action in ("safe_abort", "needs_human") and not args.terminal_reason:
            state["terminal_reason"] = args.action
    else:
        state["iteration"] = _required_int(state, "iteration") + 1
        state["cycle_phase"] = "building"
        state["build_flash_done"] = False
        state["cycle_started_monotonic_s"] = time.monotonic()
    _save_state(path, state)
    _print_state(state)
    _ok("action=%s" % args.action)
    return 0


def _cmd_capture_identity(args: argparse.Namespace) -> int:
    identity = _process_identity(args.pid)
    if identity is None:
        raise StateError("cannot verify process identity", 1)
    _print_json({"pid": args.pid, "identity": identity})
    return 0


def _cmd_deadline_check(args: argparse.Namespace) -> int:
    path, state = _mutable_state(args.runid)
    started = _optional_number(state, "cycle_started_monotonic_s")
    if started is None:
        raise StateError("cycle deadline is inactive", 1)
    now = time.monotonic() if args.now_monotonic_s is None else args.now_monotonic_s
    elapsed = now - started
    if elapsed < 0:
        raise StateError("cycle deadline clock moved backwards", 1)
    deadline = _required_number(state, "cycle_deadline_s")
    if elapsed + 1e-9 < deadline:
        _print_json({"triggered": False})
        return 0
    _terminate_capture(state)
    _clear_capture_fields(state)
    state["pending_action"] = None
    state["pending_action_converged"] = None
    state["pending_action_window_ok"] = None
    state["last_action"] = "needs_human"
    state["terminal"] = True
    state["terminal_reason"] = "cycle_deadline_exceeded"
    _save_state(path, state)
    _print_json({"action": "needs_human", "terminal_reason": "cycle_deadline_exceeded"})
    return 3


def _cmd_is_terminal(args: argparse.Namespace) -> int:
    _, state = _load_state(args.runid)
    _print_json({
        "terminal": _required_bool(state, "terminal"),
        "terminal_reason": _optional_string(state, "terminal_reason"),
    })
    return 3 if _required_bool(state, "terminal") else 0


def _cmd_resume_check(args: argparse.Namespace) -> int:
    path, state = _load_state(args.runid)
    if _sha256(Path(args.goal_path)) != _required_string(state, "goal_id").lower():
        raise StateError("goal identity mismatch", 1)
    if _required_bool(state, "terminal"):
        _print_state(state)
        return 3
    if state["cycle_phase"] == "flash_started" and state["build_flash_done"] is False:
        _terminate_capture(state)
        _clear_capture_fields(state)
        state["last_action"] = "needs_human"
        state["terminal"] = True
        state["terminal_reason"] = "flash_interrupted"
        _save_state(path, state)
        _print_json({"action": "needs_human", "terminal_reason": "flash_interrupted"})
        return 3
    if state["serial_capture_pid"] is not None and not _capture_is_owned(state):
        _clear_capture_fields(state)
        _save_state(path, state)
    _print_state(state)
    _ok("resume check passed")
    return 0


def _cmd_failsafe_check(args: argparse.Namespace) -> int:
    _, state = _load_state(args.runid)
    thresholds = _goal_thresholds(Path(args.goal), state)
    reason = _failsafe_reason(state, thresholds)
    if reason is None:
        _print_json({"triggered": False})
        return 0
    _print_json({"triggered": True, "reason": reason})
    return 3


def _cmd_next_action(args: argparse.Namespace) -> int:
    path, state = _mutable_state(args.runid)
    thresholds = _goal_thresholds(Path(args.goal), state)
    reason = _failsafe_reason(state, thresholds)
    if reason is None and args.camera_index_now is not None:
        if args.camera_index_now != _required_int(state, "camera_index"):
            reason = "camera_identity"
    if reason is None and args.camera_name_now is not None:
        if args.camera_name_now != _required_string(state, "camera_name"):
            reason = "camera_identity"
    if reason is None and _required_int(state, "iteration") >= _required_int(state, "max_iterations", 1):
        reason = "iteration_exhausted"
    if reason is None and _required_string(state, "cycle_phase") != "decided":
        raise StateError("next-action requires decided phase", 1)
    if reason is not None:
        action = "needs_human"
    elif args.converged:
        action = "success"
    elif args.window_ok:
        action = "candidate_success"
    else:
        action = "continue"
    state["pending_action"] = action
    state["pending_action_converged"] = args.converged
    state["pending_action_window_ok"] = args.window_ok
    _save_state(path, state)
    payload = {"action": action}
    if reason is not None:
        payload["terminal_reason"] = reason
    _print_json(payload)
    return 3 if action == "needs_human" else 0


def _add_runid(parser: argparse.ArgumentParser) -> None:
    parser.add_argument("--runid", required=True, type=_parse_runid)


def _build_parser() -> argparse.ArgumentParser:
    parser = StateParser(description="Authoritative Aux-Eye run-state manager")
    subparsers = parser.add_subparsers(dest="command", required=True)
    init = subparsers.add_parser("init")
    _add_runid(init)
    init.add_argument("--goal-id", required=True)
    init.add_argument("--goal-path", required=True)
    init.add_argument("--camera-index", required=True, type=int)
    init.add_argument("--camera-name", required=True)
    init.add_argument("--serial-device", required=True)
    init.add_argument("--serial-baud", required=True, type=_positive_int)
    init.add_argument("--max-iterations", required=True, type=_positive_int)
    init.add_argument("--cycle-deadline-s", type=_positive_float, default=DEFAULT_CYCLE_DEADLINE_S)
    init.set_defaults(handler=_cmd_init)
    for name, handler in (
        ("get", _cmd_get), ("incr-stability", _cmd_incr_stability),
        ("reset-stability", _cmd_reset_stability), ("incr-boot", _cmd_incr_boot),
        ("reset-boot", _cmd_reset_boot), ("incr-visibility-loss", _cmd_incr_visibility),
        ("reset-visibility-loss", _cmd_reset_visibility),
        ("reset-silence", _cmd_reset_silence),
        ("set-serial-ready", _cmd_set_serial_ready),
        ("clear-serial-capture", _cmd_clear_serial_capture),
        ("set-flash-done", _cmd_set_flash_done), ("is-terminal", _cmd_is_terminal),
    ):
        command = subparsers.add_parser(name)
        _add_runid(command)
        command.set_defaults(handler=handler)
    set_phase = subparsers.add_parser("set-phase")
    _add_runid(set_phase)
    set_phase.add_argument("--phase", required=True, choices=PHASES)
    set_phase.set_defaults(handler=_cmd_set_phase)
    add_silence = subparsers.add_parser("add-silence")
    _add_runid(add_silence)
    add_silence.add_argument("--seconds", required=True, type=_nonnegative_float)
    add_silence.set_defaults(handler=_cmd_add_silence)
    set_capture = subparsers.add_parser("set-serial-capture")
    _add_runid(set_capture)
    set_capture.add_argument("--pid", required=True, type=_positive_int)
    set_capture.add_argument("--identity", required=True)
    set_capture.add_argument("--stdout", required=True)
    set_capture.add_argument("--stderr", required=True)
    set_capture.set_defaults(handler=_cmd_set_serial_capture)
    advance = subparsers.add_parser("advance")
    _add_runid(advance)
    advance.add_argument("--action", required=True, choices=ACTIONS)
    advance.add_argument("--terminal-reason")
    advance.set_defaults(handler=_cmd_advance)
    resume = subparsers.add_parser("resume-check")
    _add_runid(resume)
    resume.add_argument("--goal-path", required=True)
    resume.set_defaults(handler=_cmd_resume_check)
    failsafe = subparsers.add_parser("failsafe-check")
    _add_runid(failsafe)
    failsafe.add_argument("--goal", required=True)
    failsafe.set_defaults(handler=_cmd_failsafe_check)
    next_action = subparsers.add_parser("next-action")
    _add_runid(next_action)
    next_action.add_argument("--goal", required=True)
    next_action.add_argument("--converged", required=True, type=_parse_bool)
    next_action.add_argument("--window-ok", required=True, type=_parse_bool)
    next_action.add_argument("--camera-index-now", type=int)
    next_action.add_argument("--camera-name-now")
    next_action.set_defaults(handler=_cmd_next_action)
    capture_identity = subparsers.add_parser("capture-identity")
    capture_identity.add_argument("--pid", required=True, type=_positive_int)
    capture_identity.set_defaults(handler=_cmd_capture_identity)
    deadline_check = subparsers.add_parser("deadline-check")
    _add_runid(deadline_check)
    deadline_check.add_argument("--now-monotonic-s", type=_nonnegative_float)
    deadline_check.set_defaults(handler=_cmd_deadline_check)
    return parser


def main() -> int:
    args = _build_parser().parse_args()
    try:
        return args.handler(args)
    except StateError as exc:
        print("[ERR][state] %s" % exc, file=sys.stderr)
        return exc.exit_code
    except OSError as exc:
        print("[ERR][state] filesystem error: %s" % exc, file=sys.stderr)
        return 2


if __name__ == "__main__":
    raise SystemExit(main())
