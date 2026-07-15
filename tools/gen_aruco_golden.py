#!/usr/bin/env python3
"""
ArUco Golden-Image Generator — 生成已知偏航角的 ArUco 测试基线图

用途：合成一张"数学上可证明"偏航角的 ArUco 标记图,供 tests/aux-eye/test_aruco.py
      做 ±2° 断言。默认生成 45° 偏航、存到 tests/fixtures/aux-eye/aruco-45deg.jpg。

生成原理(ground-truth 可证明,非目测):
  1. cv2.aruco.generateImageMarker 渲染一张"正对相机"(flat)的规范标记 tile
     (标记四周留白 quiet zone,ArUco 检测器需要)。
  2. 把 tile 的四角当作标记平面上一组已知 3D 物点(marker frame, Z=0 平面),
     绕相机 Y 轴精确旋转 YAW 度(Ry 旋转矩阵),平移到相机前 Z 处。
  3. 用针孔相机内参 K 把这组旋转后的 3D 点投影到像素平面,得到目标四角像素坐标。
  4. findHomography(tile 原始四角 → 目标四角)得到单应,warpPerspective 把 tile
     贴进白底帧 —— 于是画面里的标记恰好是"绕 Y 轴旋转 YAW 度"的透视投影。
  真实偏航角 = 命令行 --yaw(默认 45.0),因为它就是我们施加的旋转矩阵的角度,
  而不是事后目测。测试端用 solvePnP 反解应落在 [YAW-2, YAW+2]。

用法:
  python3 tools/gen_aruco_golden.py                       # 45°,写默认路径
  python3 tools/gen_aruco_golden.py --yaw 30              # 换角度
  python3 tools/gen_aruco_golden.py --out /tmp/x.jpg      # 换输出路径

依赖:opencv-contrib-python + numpy(见 tools/requirements-vision.txt)
"""

import argparse
import sys
from pathlib import Path

import cv2
import numpy as np

# 与 tools/aruco_pose.py 默认一致的合成相机内参 —— 仅用于生成基线,不代表真实标定。
FRAME_W = 1280
FRAME_H = 720
FOCAL = 1100.0          # 焦距(px)
MARKER_PX = 400         # flat tile 里标记本体的边长(px)
QUIET_ZONE_PX = 100     # 标记四周留白(px),ArUco 检测器所需
Z_DISTANCE = 0.65       # 标记到相机的距离(任意单位)
MARKER_SIDE = 0.20      # 标记物理边长(与 Z 同单位)
DEFAULT_ID = 23


def build_yaw_frame(dictionary, marker_id: int, yaw_deg: float) -> np.ndarray:
    """合成一帧:一个绕相机 Y 轴旋转 yaw_deg 度的 ArUco 标记贴在白底上。"""
    cx, cy = FRAME_W / 2.0, FRAME_H / 2.0
    k = np.array([[FOCAL, 0, cx], [0, FOCAL, cy], [0, 0, 1]], dtype=np.float64)

    marker = cv2.aruco.generateImageMarker(dictionary, marker_id, MARKER_PX)
    tile_side = MARKER_PX + 2 * QUIET_ZONE_PX
    tile = np.full((tile_side, tile_side), 255, dtype=np.uint8)
    tile[QUIET_ZONE_PX:QUIET_ZONE_PX + MARKER_PX,
         QUIET_ZONE_PX:QUIET_ZONE_PX + MARKER_PX] = marker

    # tile 四角对应的标记平面 3D 物点(Y-down,与图像坐标系一致;顺序 TL,TR,BR,BL)。
    half = MARKER_SIDE / 2.0
    half_tile = half * (tile_side / float(MARKER_PX))
    obj_tile = np.array([
        [-half_tile, -half_tile, 0.0],
        [half_tile, -half_tile, 0.0],
        [half_tile, half_tile, 0.0],
        [-half_tile, half_tile, 0.0],
    ], dtype=np.float64)

    theta = np.deg2rad(yaw_deg)
    r_y = np.array([
        [np.cos(theta), 0.0, np.sin(theta)],
        [0.0, 1.0, 0.0],
        [-np.sin(theta), 0.0, np.cos(theta)],
    ], dtype=np.float64)
    tvec = np.array([[0.0], [0.0], [Z_DISTANCE]], dtype=np.float64)

    cam_pts = (r_y @ obj_tile.T) + tvec
    proj = k @ cam_pts
    proj = (proj[:2] / proj[2]).T  # 4x2 目标像素坐标

    src = np.array([
        [0, 0], [tile_side, 0], [tile_side, tile_side], [0, tile_side],
    ], dtype=np.float32)
    homography, _ = cv2.findHomography(src, proj.astype(np.float32))

    frame = np.full((FRAME_H, FRAME_W), 255, dtype=np.uint8)
    warped = cv2.warpPerspective(
        tile, homography, (FRAME_W, FRAME_H),
        flags=cv2.INTER_LINEAR, borderValue=255)
    mask = cv2.warpPerspective(
        np.full((tile_side, tile_side), 255, np.uint8), homography,
        (FRAME_W, FRAME_H), flags=cv2.INTER_NEAREST)
    frame[mask > 0] = warped[mask > 0]
    return cv2.cvtColor(frame, cv2.COLOR_GRAY2BGR)


def main() -> int:
    default_out = (Path(__file__).resolve().parents[1]
                   / "tests" / "fixtures" / "aux-eye" / "aruco-45deg.jpg")
    parser = argparse.ArgumentParser(
        description="Generate a golden ArUco image with a known yaw angle.")
    parser.add_argument("--yaw", type=float, default=45.0,
                        help="ground-truth yaw in degrees (default: 45.0)")
    parser.add_argument("--dict", default="DICT_4X4_50",
                        help="ArUco dictionary name (default: DICT_4X4_50)")
    parser.add_argument("--id", type=int, default=DEFAULT_ID,
                        help="marker id (default: %d)" % DEFAULT_ID)
    parser.add_argument("--out", default=str(default_out),
                        help="output JPEG path")
    args = parser.parse_args()

    dict_id = getattr(cv2.aruco, args.dict, None)
    if dict_id is None:
        print("[ERR][aruco-gen] unknown dictionary: %s" % args.dict,
              file=sys.stderr)
        return 1
    dictionary = cv2.aruco.getPredefinedDictionary(dict_id)

    frame = build_yaw_frame(dictionary, args.id, args.yaw)

    # 自检:确认生成的图确实能被检测到,否则基线无意义。
    detector = cv2.aruco.ArucoDetector(dictionary, cv2.aruco.DetectorParameters())
    corners, ids, _ = detector.detectMarkers(frame)
    if ids is None:
        print("[ERR][aruco-gen] generated image is not detectable — aborting",
              file=sys.stderr)
        return 1

    out_path = Path(args.out)
    out_path.parent.mkdir(parents=True, exist_ok=True)
    ok = cv2.imwrite(str(out_path), frame, [cv2.IMWRITE_JPEG_QUALITY, 95])
    if not ok:
        print("[ERR][aruco-gen] failed to write %s" % out_path, file=sys.stderr)
        return 1

    marker_w = float(np.linalg.norm(
        corners[0].reshape(4, 2)[1] - corners[0].reshape(4, 2)[0]))
    print("[OK][aruco-gen] wrote %s" % out_path, file=sys.stderr)
    print("[OK][aruco-gen] yaw=%.1f id=%d detected_id=%s marker_top_edge=%.0fpx (%.1f%% of frame width)"
          % (args.yaw, args.id, ids.flatten().tolist(),
             marker_w, 100.0 * marker_w / FRAME_W), file=sys.stderr)
    return 0


if __name__ == "__main__":
    sys.exit(main())
