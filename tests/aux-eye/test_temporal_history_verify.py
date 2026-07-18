from __future__ import annotations

import hashlib
import json
import shutil
import subprocess
import sys
from pathlib import Path

import pytest


_ROOT = Path(__file__).resolve().parents[2]
_VERIFY = _ROOT / "tools" / "aux-eye" / "verify_aux_eye_temporal.py"
_FIXTURE = _ROOT / "tests" / "fixtures" / "aux-eye" / "temporal-still"


def _observation(timestamps: tuple[int, int, int]) -> dict:
    frames = []
    for index, timestamp in enumerate(timestamps):
        path = Path("frames") / f"{index}.jpg"
        frames.append({"path": path.as_posix(), "sha256": _sha(path), "ts": timestamp})
    return {"frames": frames, "visible": False}


def _sha(path: Path) -> str:
    return hashlib.sha256((_FIXTURE / path).read_bytes()).hexdigest()


def _event(confirmations: int, status: str = "detected") -> dict:
    return {
        "kind": "oscillation",
        "status": status,
        "start_frame": 0,
        "end_frame": 2,
        "confirmations": confirmations,
    }


def _write_window(history: Path, name: str, observation: dict) -> None:
    window = history / name
    shutil.copytree(_FIXTURE / "frames", window / "frames")
    (window / "observation.json").write_text(json.dumps(observation), encoding="utf-8")


def _run(observation: dict, history: Path) -> subprocess.CompletedProcess[str]:
    return subprocess.run(
        [
            sys.executable,
            str(_VERIFY),
            "--sequence-dir",
            str(_FIXTURE),
            "--observation",
            "-",
            "--history",
            str(history),
        ],
        input=json.dumps(observation),
        text=True,
        capture_output=True,
        cwd=_ROOT,
        check=False,
    )


def _current() -> dict:
    value = _observation((7, 8, 9))
    value["temporal_events"] = [_event(3)]
    return value


def test_history_allows_only_traceable_consecutive_confirmation(tmp_path: Path):
    history = tmp_path / "history"
    history.mkdir()
    for index in range(2):
        previous = _observation((index * 3 + 1, index * 3 + 2, index * 3 + 3))
        previous["temporal_events"] = [_event(index + 1)]
        _write_window(history, f"{index:02}", previous)

    accepted = _run(_current(), history)
    rejected = _current()
    rejected["temporal_events"] = [_event(4)]
    result = _run(rejected, history)

    assert accepted.returncode == 0, accepted.stderr
    assert result.returncode == 1
    assert "confirmations" in result.stderr.lower()


@pytest.mark.parametrize("status", ["not_detected", "indeterminate"])
def test_non_detected_history_does_not_authorize_confirmations(
    tmp_path: Path, status: str
):
    history = tmp_path / "history"
    history.mkdir()
    for index in range(2):
        previous = _observation((index * 3 + 1, index * 3 + 2, index * 3 + 3))
        previous["temporal_events"] = [_event(0, status)]
        _write_window(history, f"{index:02}", previous)

    result = _run(_current(), history)

    assert result.returncode == 1
    assert result.stdout.strip() == "FAIL"
    assert "confirmations" in result.stderr.lower()


def test_newest_non_detected_history_breaks_detected_streak(tmp_path: Path):
    history = tmp_path / "history"
    history.mkdir()
    for index, status in enumerate(("detected", "not_detected")):
        previous = _observation((index * 3 + 1, index * 3 + 2, index * 3 + 3))
        previous["temporal_events"] = [_event(1 if status == "detected" else 0, status)]
        _write_window(history, f"{index:02}", previous)
    current = _current()
    current["temporal_events"] = [_event(2)]

    result = _run(current, history)

    assert result.returncode == 1
    assert "confirmations" in result.stderr.lower()


@pytest.mark.parametrize(
    ("name", "observation", "fragment"),
    [
        ("schema", {"temporal_events": [{"kind": "oscillation"}]}, "schema-invalid"),
        ("forged", None, "confirmations"),
    ],
)
def test_invalid_history_window_is_input_failure(
    tmp_path: Path, name: str, observation: dict | None, fragment: str
):
    history = tmp_path / "history"
    history.mkdir()
    if observation is None:
        observation = _observation((1, 2, 3))
        observation["temporal_events"] = [_event(99)]
        _write_window(history, name, observation)
    else:
        window = history / name
        window.mkdir()
        (window / "observation.json").write_text(json.dumps(observation), encoding="utf-8")

    result = _run(_current(), history)

    assert result.returncode == 2
    assert result.stdout == ""
    assert "history" in result.stderr.lower()
    assert fragment in result.stderr.lower()


def test_understated_detected_history_confirmation_is_input_failure(tmp_path: Path):
    history = tmp_path / "history"
    history.mkdir()
    previous = _observation((1, 2, 3))
    previous["temporal_events"] = [_event(0)]
    _write_window(history, "understated", previous)
    current = _observation((4, 5, 6))
    current["temporal_events"] = [_event(2)]

    result = _run(current, history)

    assert result.returncode == 2
    assert result.stdout == ""
    assert "history" in result.stderr.lower()
    assert "confirmations" in result.stderr.lower()


def test_non_detected_history_confirmation_must_be_zero(tmp_path: Path):
    history = tmp_path / "history"
    history.mkdir()
    previous = _observation((1, 2, 3))
    previous["temporal_events"] = [_event(1, "not_detected")]
    _write_window(history, "forged", previous)
    current = _observation((4, 5, 6))
    current["temporal_events"] = [_event(1)]

    result = _run(current, history)

    assert result.returncode == 2
    assert result.stdout == ""
    assert "history" in result.stderr.lower()
    assert "confirmations" in result.stderr.lower()


def test_history_window_with_tampered_frame_identity_is_input_failure(tmp_path: Path):
    history = tmp_path / "history"
    history.mkdir()
    previous = _observation((1, 2, 3))
    previous["frames"][0]["sha256"] = "0" * 64
    previous["temporal_events"] = [_event(1)]
    _write_window(history, "window", previous)
    result = _run(_current(), history)

    assert result.returncode == 2
    assert result.stdout == ""
    assert "history" in result.stderr.lower()
    assert "sha256" in result.stderr.lower()


def test_history_uses_timestamps_not_window_names_for_detected_chain(tmp_path: Path):
    history = tmp_path / "history"
    history.mkdir()
    older = _observation((1, 2, 3))
    older["temporal_events"] = [_event(1)]
    _write_window(history, "z-older", older)
    newer = _observation((4, 5, 6))
    newer["temporal_events"] = [_event(2)]
    _write_window(history, "a-newer", newer)

    result = _run(_current(), history)

    assert result.returncode == 0, result.stderr


def test_newest_timestamped_non_detection_breaks_a_maliciously_named_chain(
    tmp_path: Path,
):
    history = tmp_path / "history"
    history.mkdir()
    older = _observation((1, 2, 3))
    older["temporal_events"] = [_event(1)]
    _write_window(history, "z-older-detected", older)
    newer = _observation((4, 5, 6))
    newer["temporal_events"] = [_event(0, "not_detected")]
    _write_window(history, "a-newer-not-detected", newer)
    current = _current()
    current["temporal_events"] = [_event(2)]

    result = _run(current, history)

    assert result.returncode == 1
    assert result.stdout.strip() == "FAIL"
    assert "confirmations" in result.stderr.lower()


@pytest.mark.parametrize(
    ("windows", "fragment"),
    [
        ((("first", (1, 2, 3)), ("duplicate", (1, 2, 3))), "chronology"),
        ((("first", (1, 2, 3)), ("reversed", (6, 5, 4))), "timestamp order"),
    ],
)
def test_duplicate_or_non_monotonic_history_chronology_is_input_failure(
    tmp_path: Path, windows, fragment: str
):
    history = tmp_path / "history"
    history.mkdir()
    for name, timestamps in windows:
        previous = _observation(timestamps)
        previous["temporal_events"] = [_event(1)]
        _write_window(history, name, previous)

    result = _run(_current(), history)

    assert result.returncode == 2
    assert result.stdout == ""
    assert "history" in result.stderr.lower()
    assert fragment in result.stderr.lower()


def test_current_window_must_follow_validated_history(tmp_path: Path):
    history = tmp_path / "history"
    history.mkdir()
    previous = _observation((7, 8, 9))
    previous["temporal_events"] = [_event(1)]
    _write_window(history, "later", previous)

    result = _run(_current(), history)

    assert result.returncode == 2
    assert result.stdout == ""
    assert "history chronology" in result.stderr.lower()


def test_flat_history_cannot_supply_an_unverifiable_sequence_context(tmp_path: Path):
    history = tmp_path / "history"
    history.mkdir()
    previous = _observation((1, 2, 3))
    previous["temporal_events"] = [_event(1)]
    (history / "flat.json").write_text(json.dumps(previous), encoding="utf-8")

    result = _run(_current(), history)

    assert result.returncode == 2
    assert result.stdout == ""
    assert "history" in result.stderr.lower()
