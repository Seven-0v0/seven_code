"""
pytest for tools/aruco_pose.py —— ArUco 单标记偏航角断言。

覆盖:
  1. golden 45° 图 -> detected=true 且 yaw_deg ∈ [43, 47](±2° 容差,计划规定)。
  2. 无标记帧(dark.jpg)-> detected=false,不崩,退出码 0。

golden 图由 tools/gen_aruco_golden.py 以数学上可证明的 45° 偏航合成
(绕相机 Y 轴施加 45° 旋转矩阵后透视投影),故 ground-truth = 45°,非目测。
"""

import json
import subprocess
import sys
from pathlib import Path

import pytest

_REPO_ROOT = Path(__file__).resolve().parents[2]
_ARUCO_POSE = _REPO_ROOT / "tools" / "aruco_pose.py"

# golden 图的 ground-truth 偏航角与计划规定的 ±2° 容差。
_GOLDEN_YAW = 45.0
_TOLERANCE = 2.0
_YAW_LO = _GOLDEN_YAW - _TOLERANCE  # 43
_YAW_HI = _GOLDEN_YAW + _TOLERANCE  # 47


def _run_aruco_pose(frame_path: str) -> subprocess.CompletedProcess:
    return subprocess.run(
        [sys.executable, str(_ARUCO_POSE), "--frame", frame_path],
        capture_output=True, text=True, cwd=str(_REPO_ROOT))


def test_golden_45deg_yaw_within_tolerance(fixtures_dir):
    """45° golden 图测出的偏航角应落在 [43, 47]。"""
    golden = Path(fixtures_dir) / "aruco-45deg.jpg"
    assert golden.is_file(), "golden fixture missing: %s" % golden
    assert golden.stat().st_size > 0, "golden fixture is empty (placeholder?)"

    result = _run_aruco_pose(str(golden))
    assert result.returncode == 0, "stderr: %s" % result.stderr

    payload = json.loads(result.stdout.strip())
    assert payload["detected"] is True, "marker should be detected in golden image"
    assert payload["marker_id"] is not None
    yaw = payload["yaw_deg"]
    assert yaw is not None, "yaw_deg should be present when detected"
    assert _YAW_LO <= yaw <= _YAW_HI, (
        "measured yaw %.2f not within [%.0f, %.0f]" % (yaw, _YAW_LO, _YAW_HI))


def test_no_marker_frame_graceful(fixtures_dir):
    """无标记帧应优雅返回 detected=false、退出码 0,绝不崩溃。"""
    dark = Path(fixtures_dir) / "dark.jpg"
    assert dark.is_file(), "dark fixture missing: %s" % dark

    result = _run_aruco_pose(str(dark))
    assert result.returncode == 0, "should exit 0 on marker-free frame; stderr: %s" % result.stderr

    payload = json.loads(result.stdout.strip())
    assert payload["detected"] is False
    assert payload["marker_id"] is None
    assert payload["yaw_deg"] is None


def test_missing_frame_exits_nonzero_without_crash():
    """帧不存在时应报错退出(码 1),而非抛未捕获异常。"""
    result = _run_aruco_pose("/nonexistent/frame.jpg")
    assert result.returncode == 1
    assert "not found" in result.stderr.lower()
