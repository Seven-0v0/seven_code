#!/usr/bin/env python3
"""
ArUco Pose Tool — 可选 ArUco 定量姿态(单标记偏航角)

用途:在给定一帧图像里检测**单个** ArUco 标记,输出相对相机平面法线的偏航角
      (yaw,绕相机竖直 Y 轴)。这是辅眼默认 VLM 路径的**可选增强**——有标记时叠加
      定量姿态,无标记时优雅返回 detected:false(退出码 0,不崩)。

      默认 VLM 感知路径(camera_capture.py / verify_aux_eye.py)不依赖本模块。

Day-1 范围(刻意最小):单标记检测 + 偏航角。
  不做:标记平滑、多标记三角化、相机标定编写、实时视频叠加。

用法:
  python3 tools/aruco_pose.py --frame <帧路径>
  python3 tools/aruco_pose.py --frame <帧路径> --dict DICT_4X4_50

输出(stdout,单行 JSON):
  {"marker_id": <int|null>, "yaw_deg": <float|null>, "detected": <bool>}
状态信息打到 stderr([OK]/[WARN]/[ERR][aruco])。

退出码:
  0  正常(检测到标记,或确认无标记 —— 两者都不是错误)
  1  用法错误(帧不存在/无法读取/未知字典)

依赖:opencv-contrib-python + numpy(见 tools/requirements-vision.txt)

偏航角说明:
  无相机标定时,姿态是"标定无关"的近似 —— 用一组合成针孔内参
  (焦距 = 帧宽,主点 = 帧中心,零畸变)+ 单位边长标记跑 solvePnP。
  在正对/中心场景下偏航角量级可靠(±2°),但绝对值受未知真实焦距影响;
  这符合 day-1"量级参考"定位,不作精确标定测量用途。
"""

import argparse
import json
import sys
from pathlib import Path

import cv2
import numpy as np

# 标定无关的单位边长标记物点(marker frame, Z=0),顺序 TL,TR,BR,BL,Y-down
# 与 cv2.aruco 输出的角点顺序一致。
_HALF = 0.5
_OBJECT_POINTS = np.array([
    [-_HALF, -_HALF, 0.0],
    [_HALF, -_HALF, 0.0],
    [_HALF, _HALF, 0.0],
    [-_HALF, _HALF, 0.0],
], dtype=np.float64)


def _log(msg: str) -> None:
    print(msg, file=sys.stderr)


def _emit(marker_id, yaw_deg, detected: bool) -> None:
    """把结果打到 stdout(单行 JSON,机器可消费)。"""
    print(json.dumps({
        "marker_id": marker_id,
        "yaw_deg": yaw_deg,
        "detected": detected,
    }))


def _compute_yaw_deg(corners: np.ndarray, frame_shape) -> float:
    """
    从标记四角像素坐标反解相对相机平面法线的偏航角(度)。

    用合成针孔内参(焦距 = 帧宽,主点 = 帧中心,零畸变)跑 IPPE_SQUARE solvePnP,
    得到标记相对相机的旋转,取其法线在相机 XZ 平面上的方位角作为 yaw。
    IPPE 对平面标记有法线正负两解,故把法线翻到朝向相机一侧后再取角。
    """
    h, w = frame_shape[:2]
    focal = float(w)  # 无标定时的合理默认(视场角 ~ 长边)
    k = np.array([
        [focal, 0.0, w / 2.0],
        [0.0, focal, h / 2.0],
        [0.0, 0.0, 1.0],
    ], dtype=np.float64)
    dist = np.zeros((5, 1), dtype=np.float64)

    image_points = corners.reshape(4, 2).astype(np.float64)
    ok, rvec, _ = cv2.solvePnP(
        _OBJECT_POINTS, image_points, k, dist,
        flags=cv2.SOLVEPNP_IPPE_SQUARE)
    if not ok:
        # 退回一般 PnP(极少发生);仍失败则抛出交由上层处理。
        ok, rvec, _ = cv2.solvePnP(_OBJECT_POINTS, image_points, k, dist)
        if not ok:
            raise cv2.error("solvePnP failed to converge")

    rot, _ = cv2.Rodrigues(rvec)
    normal = rot @ np.array([0.0, 0.0, 1.0])
    if normal[2] > 0:  # 让法线朝向相机(-Z 方向)
        normal = -normal
    return float(np.rad2deg(np.arctan2(normal[0], -normal[2])))


def detect_pose(frame: np.ndarray, dict_name: str):
    """
    返回 (marker_id, yaw_deg, detected)。
    多个标记时取第一个(day-1 单标记范围);无标记 -> (None, None, False)。
    """
    dict_id = getattr(cv2.aruco, dict_name, None)
    if dict_id is None:
        raise ValueError("unknown ArUco dictionary: %s" % dict_name)
    dictionary = cv2.aruco.getPredefinedDictionary(dict_id)
    detector = cv2.aruco.ArucoDetector(
        dictionary, cv2.aruco.DetectorParameters())

    corners, ids, _ = detector.detectMarkers(frame)
    if ids is None or len(ids) == 0:
        return None, None, False

    if len(ids) > 1:
        _log("[WARN][aruco] %d markers found; day-1 scope reports the first only"
             % len(ids))

    marker_id = int(ids.flatten()[0])
    try:
        yaw = _compute_yaw_deg(corners[0], frame.shape)
    except cv2.error as exc:
        _log("[WARN][aruco] pose solve failed: %s" % exc)
        return marker_id, None, True
    return marker_id, round(yaw, 2), True


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Detect a single ArUco marker and report its yaw angle.")
    parser.add_argument("--frame", required=True, help="path to a frame image")
    parser.add_argument("--dict", default="DICT_4X4_50",
                        help="ArUco dictionary name (default: DICT_4X4_50)")
    args = parser.parse_args()

    frame_path = Path(args.frame)
    if not frame_path.is_file():
        _log("[ERR][aruco] frame not found: %s" % frame_path)
        return 1

    frame = cv2.imread(str(frame_path))
    if frame is None:
        _log("[ERR][aruco] could not read image: %s" % frame_path)
        return 1

    try:
        marker_id, yaw_deg, detected = detect_pose(frame, args.dict)
    except ValueError as exc:
        _log("[ERR][aruco] %s" % exc)
        return 1

    if detected:
        _log("[OK][aruco] marker_id=%s yaw_deg=%s" % (marker_id, yaw_deg))
    else:
        _log("[OK][aruco] no marker detected in frame")
    _emit(marker_id, yaw_deg, detected)
    return 0


if __name__ == "__main__":
    sys.exit(main())
