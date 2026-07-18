"""
pytest for tools/aux-eye/verify_aux_eye.py —— 观测校验器 + 确定性门。

覆盖:
  1. 合法观测(source_frame_sha256 与真实帧 sha256 匹配)-> 通过,exit 0。
  2. source_frame_sha256 被篡改 -> 拒绝,exit 1,输出提到 "sha256"。
  3. dark.jpg fixture 的合法观测(visible=false,failure_reason=dark)-> 通过,exit 0。

设计说明:
  - 帧 sha256 在测试里用 hashlib 从真实 fixture 文件动态算出,绝不硬编码/伪造。
  - 用受版本库跟踪的 tests/fixtures/aux-eye/*.jpg(而非 .omo/evidence/frames/,后者被 gitignore),
    保证测试在干净检出/CI 里也能跑。
  - 校验器只消费已产出的观测 JSON,自身不读图、不联网(纯确定性门)。
"""

import hashlib
import json
import subprocess
import sys
from pathlib import Path

import pytest

_REPO_ROOT = Path(__file__).resolve().parents[2]
_VERIFY = _REPO_ROOT / "tools" / "aux-eye" / "verify_aux_eye.py"


def _sha256(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def _run_verify(frame: str, observation: str) -> subprocess.CompletedProcess:
    """把观测 JSON 从 stdin 喂给校验器(--observation -)。"""
    return subprocess.run(
        [sys.executable, str(_VERIFY), "--frame", frame, "--observation", "-"],
        input=observation, capture_output=True, text=True, cwd=str(_REPO_ROOT))


def _run_verify_file(frame: str, obs_path: str) -> subprocess.CompletedProcess:
    return subprocess.run(
        [sys.executable, str(_VERIFY), "--frame", frame, "--observation", obs_path],
        capture_output=True, text=True, cwd=str(_REPO_ROOT))


def test_help_exits_zero():
    """--help 应 exit 0。"""
    result = subprocess.run(
        [sys.executable, str(_VERIFY), "--help"],
        capture_output=True, text=True, cwd=str(_REPO_ROOT))
    assert result.returncode == 0


def test_legal_observation_matching_real_frame_passes(fixtures_dir):
    """合法观测(sha256 匹配真实帧,visible=true 且无 objects)-> exit 0。

    同时证明 len(objects)>=1 不是无条件要求(objects 缺省但 visible=true 仍通过)。
    """
    frame = Path(fixtures_dir) / "mug.jpg"
    assert frame.is_file(), "fixture missing: %s" % frame

    observation = json.dumps({
        "source_frame_sha256": _sha256(frame),
        "visible": True,
        "failure_reason": "none",
    })
    result = _run_verify(str(frame), observation)
    assert result.returncode == 0, (
        "legal observation should pass; stdout=%s stderr=%s"
        % (result.stdout, result.stderr))


def test_legal_observation_with_objects_and_confidence_passes(fixtures_dir):
    """含 objects + confidence 的合法观测(confidence∈[0,1])-> exit 0。"""
    frame = Path(fixtures_dir) / "mug.jpg"
    observation = json.dumps({
        "source_frame_sha256": _sha256(frame),
        "visible": True,
        "failure_reason": "none",
        "scene": {"subject": "a mug on a desk"},
        "objects": [{"name": "mug", "confidence": 0.8}],
        "confidence": 0.7,
    })
    result = _run_verify(str(frame), observation)
    assert result.returncode == 0, (
        "legal observation with objects should pass; stderr=%s" % result.stderr)


def test_tampered_source_frame_sha256_fails_and_mentions_sha256(fixtures_dir):
    """篡改 source_frame_sha256 -> exit 1 且输出提到 'sha256'(frame-identity 门)。"""
    frame = Path(fixtures_dir) / "mug.jpg"
    tampered = "0" * 64  # 合法格式(64 hex)但与真实帧不匹配
    assert tampered != _sha256(frame)

    observation = json.dumps({
        "source_frame_sha256": tampered,
        "visible": True,
        "failure_reason": "none",
    })
    result = _run_verify(str(frame), observation)
    assert result.returncode == 1, "tampered sha256 should fail with exit 1"
    combined = (result.stdout + result.stderr).lower()
    assert "sha256" in combined, (
        "failure output should mention 'sha256'; got: %s" % combined)


def test_dark_fixture_legal_observation_passes(fixtures_dir):
    """dark.jpg 的合法观测(visible=false,failure_reason=dark)-> exit 0。"""
    frame = Path(fixtures_dir) / "dark.jpg"
    assert frame.is_file(), "dark fixture missing: %s" % frame

    observation = json.dumps({
        "source_frame_sha256": _sha256(frame),
        "visible": False,
        "failure_reason": "dark",
    })
    result = _run_verify(str(frame), observation)
    assert result.returncode == 0, (
        "dark legal observation should pass; stdout=%s stderr=%s"
        % (result.stdout, result.stderr))


def test_observation_from_file_path_also_works(fixtures_dir, tmp_path):
    """--observation 支持文件路径(不只是 stdin)。"""
    frame = Path(fixtures_dir) / "dark.jpg"
    obs_path = tmp_path / "obs.json"
    obs_path.write_text(json.dumps({
        "source_frame_sha256": _sha256(frame),
        "visible": False,
        "failure_reason": "occluded",
    }))
    result = _run_verify_file(str(frame), str(obs_path))
    assert result.returncode == 0, "file-based observation should pass; stderr=%s" % result.stderr


def test_missing_source_frame_sha256_rejected_by_schema(fixtures_dir):
    """缺 source_frame_sha256 -> schema 校验失败,exit 1。"""
    frame = Path(fixtures_dir) / "mug.jpg"
    observation = json.dumps({"visible": True, "failure_reason": "none"})
    result = _run_verify(str(frame), observation)
    assert result.returncode == 1


def test_schema_invariant_violation_rejected(fixtures_dir):
    """visible=true 但 failure_reason!=none -> 违反 schema if/then/else 不变量,exit 1。"""
    frame = Path(fixtures_dir) / "mug.jpg"
    observation = json.dumps({
        "source_frame_sha256": _sha256(frame),
        "visible": True,
        "failure_reason": "dark",  # 与 visible=true 矛盾
    })
    result = _run_verify(str(frame), observation)
    assert result.returncode == 1


def test_missing_frame_file_exits_nonzero(fixtures_dir):
    """帧文件不存在 -> 报错退出(非 0),不抛未捕获异常。"""
    observation = json.dumps({
        "source_frame_sha256": "0" * 64,
        "visible": False,
        "failure_reason": "dark",
    })
    result = _run_verify("/nonexistent/frame.jpg", observation)
    assert result.returncode != 0
