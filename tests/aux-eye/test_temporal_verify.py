from __future__ import annotations

import hashlib
import json
import subprocess
import sys
from pathlib import Path

import pytest


_REPO_ROOT = Path(__file__).resolve().parents[2]
_VERIFY = _REPO_ROOT / "tools" / "aux-eye" / "verify_aux_eye_temporal.py"
_FIXTURE = _REPO_ROOT / "tests" / "fixtures" / "aux-eye" / "temporal-still"


def _sha256(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def _observation(
    *, visible: bool = False, timestamps: tuple[int | str, ...] = (1, 2, 3)
) -> dict:
    frames = []
    for index, timestamp in enumerate(timestamps):
        path = Path("frames") / f"{index}.jpg"
        frames.append(
            {
                "path": path.as_posix(),
                "sha256": _sha256(_FIXTURE / path),
                "ts": timestamp,
            }
        )
    return {"frames": frames, "visible": visible}


def _run(
    observation: dict | str,
    *,
    sequence_dir: Path = _FIXTURE,
    evidence: Path | None = None,
    history: Path | None = None,
    schema: Path | None = None,
) -> subprocess.CompletedProcess[str]:
    command = [
        sys.executable,
        str(_VERIFY),
        "--sequence-dir",
        str(sequence_dir),
        "--observation",
        "-",
    ]
    if evidence is not None:
        command.extend(("--evidence", str(evidence)))
    if history is not None:
        command.extend(("--history", str(history)))
    if schema is not None:
        command.extend(("--schema", str(schema)))
    payload = observation if isinstance(observation, str) else json.dumps(observation)
    return subprocess.run(
        command,
        input=payload,
        capture_output=True,
        text=True,
        cwd=_REPO_ROOT,
        check=False,
    )


def _event(
    confirmations: int,
    *,
    start: int = 0,
    end: int = 2,
    status: str = "detected",
) -> dict:
    return {
        "kind": "oscillation",
        "status": status,
        "start_frame": start,
        "end_frame": end,
        "confirmations": confirmations,
    }


def test_help_lists_temporal_inputs_and_exits_zero():
    result = subprocess.run(
        [sys.executable, str(_VERIFY), "--help"],
        capture_output=True,
        text=True,
        cwd=_REPO_ROOT,
        check=False,
    )

    assert result.returncode == 0
    for option in ("--sequence-dir", "--observation", "--schema", "--evidence", "--history"):
        assert option in result.stdout


def test_valid_three_frame_sequence_passes():
    result = _run(_observation())

    assert result.returncode == 0, result.stderr
    assert result.stdout.strip() == "PASS"
    assert "[OK][verify]" in result.stderr


def test_observation_file_and_schema_override_pass(tmp_path: Path):
    observation_path = tmp_path / "observation.json"
    observation_path.write_text(json.dumps(_observation()), encoding="utf-8")
    schema = _REPO_ROOT / "schemas" / "aux-eye" / "temporal.schema.json"

    result = subprocess.run(
        [
            sys.executable,
            str(_VERIFY),
            "--sequence-dir",
            str(_FIXTURE),
            "--observation",
            str(observation_path),
            "--schema",
            str(schema),
        ],
        capture_output=True,
        text=True,
        cwd=_REPO_ROOT,
        check=False,
    )

    assert result.returncode == 0, result.stderr


@pytest.mark.parametrize(
    ("mutation", "failure_fragment"),
    [
        (lambda value: value["frames"][1].update(sha256="0" * 64), "sha256"),
        (lambda value: value["frames"][1].update(ts=0), "order"),
        (lambda value: value.update(visible=True), "roll-up"),
        (lambda value: value.update(frames=[]), "schema"),
    ],
)
def test_deterministic_gate_failure_exits_one(mutation, failure_fragment: str):
    observation = _observation()
    mutation(observation)

    result = _run(observation)

    assert result.returncode == 1
    assert failure_fragment in (result.stdout + result.stderr).lower()
    assert result.stdout.strip() == "FAIL"
    assert "[ERR][verify]" in result.stderr


def test_iso_timestamps_must_be_strictly_increasing():
    observation = _observation(
        timestamps=("2026-07-16T10:00:00Z", "2026-07-16T09:59:59Z", "2026-07-16T10:00:01Z")
    )

    result = _run(observation)

    assert result.returncode == 1
    assert "order" in (result.stdout + result.stderr).lower()


@pytest.mark.parametrize("timestamp", [None, []])
def test_schema_invalid_timestamp_type_returns_controlled_failure(timestamp):
    observation = _observation()
    observation["frames"][0]["ts"] = timestamp

    result = _run(observation)

    assert result.returncode == 1
    assert result.stdout.strip() == "FAIL"
    assert "schema" in result.stderr.lower()
    assert "traceback" not in result.stderr.lower()


def test_uppercase_frame_and_embedded_sha256_claims_pass():
    observation = _observation()
    for frame in observation["frames"]:
        frame["sha256"] = frame["sha256"].upper()
        frame["observation"] = {
            "source_frame_sha256": frame["sha256"],
            "visible": False,
            "failure_reason": "empty",
        }

    result = _run(observation)

    assert result.returncode == 0, result.stderr
    assert result.stdout.strip() == "PASS"


def test_mixed_visibility_rolls_up_true():
    observation = _observation(visible=True)
    for index, frame in enumerate(observation["frames"]):
        is_visible = index == 0
        frame["observation"] = {
            "source_frame_sha256": frame["sha256"],
            "visible": is_visible,
            "failure_reason": "none" if is_visible else "empty",
        }

    result = _run(observation)

    assert result.returncode == 0, result.stderr


@pytest.mark.parametrize("path", ["/tmp/frame.jpg", "../frame.jpg", "frames/missing.jpg"])
def test_invalid_frame_path_is_input_failure(path: str):
    observation = _observation()
    observation["frames"][0]["path"] = path

    result = _run(observation)

    assert result.returncode == 2


def test_symlink_escape_is_input_failure(tmp_path: Path):
    sequence_dir = tmp_path / "sequence"
    sequence_dir.mkdir()
    outside = tmp_path / "outside.jpg"
    outside.write_bytes((_FIXTURE / "frames" / "0.jpg").read_bytes())
    (sequence_dir / "escaped.jpg").symlink_to(outside)
    observation = {
        "frames": [{"path": "escaped.jpg", "sha256": _sha256(outside), "ts": 1}],
        "visible": False,
    }

    result = _run(observation, sequence_dir=sequence_dir)

    assert result.returncode == 2


@pytest.mark.parametrize(("start", "end"), [(2, 1), (0, 3), (-1, 0)])
def test_invalid_event_frame_bounds_fail(start: int, end: int):
    observation = _observation()
    observation["temporal_events"] = [_event(1, start=start, end=end)]

    result = _run(observation)

    assert result.returncode == 1
    assert "frame-index" in (result.stdout + result.stderr).lower()


def test_first_window_rejects_confirmation_jump():
    observation = _observation()
    observation["temporal_events"] = [_event(5)]

    result = _run(observation)

    assert result.returncode == 1
    assert "confirmations" in (result.stdout + result.stderr).lower()


def test_evidence_is_append_only_json_audit(tmp_path: Path):
    evidence = tmp_path / "audit.log"

    first = _run(_observation(), evidence=evidence)
    first_size = evidence.stat().st_size
    second = _run(_observation(), evidence=evidence)
    audits = [json.loads(line) for line in evidence.read_text(encoding="utf-8").splitlines()]

    assert first.returncode == second.returncode == 0
    assert evidence.stat().st_size > first_size
    assert [audit["verdict"] for audit in audits] == ["PASS", "PASS"]


@pytest.mark.parametrize("semantic_failure", [False, True])
def test_evidence_write_failure_is_controlled_before_any_verdict(
    tmp_path: Path, semantic_failure: bool
):
    blocked_parent = tmp_path / "blocked"
    blocked_parent.write_text("not a directory", encoding="utf-8")
    observation = _observation()
    if semantic_failure:
        observation["frames"][0]["sha256"] = "0" * 64

    result = _run(observation, evidence=blocked_parent / "audit.jsonl")

    assert result.returncode == 2
    assert result.stdout == ""
    assert "[ERR][verify] evidence error:" in result.stderr
    assert "traceback" not in result.stderr.lower()


def test_unparseable_observation_is_input_failure():
    result = _run("{")

    assert result.returncode == 2
