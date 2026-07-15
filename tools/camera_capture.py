#!/usr/bin/env python3
"""
macOS Camera Capture Tool — AI 可调用的纯采集脚本(零感知逻辑、零网络 I/O)

用途：打开指定摄像头，抓取一帧或多帧画面存为 JPEG，并把每帧的元信息
      (路径 / sha256 / 相机 index / 时间戳 / 宽高) 以 NDJSON 形式输出到 stdout。
      本工具只负责"把画面存到磁盘"，看图理解由上层 agent 读磁盘帧完成——
      本文件不做任何画面理解，也不发起任何网络调用。

用法：
  python3 camera_capture.py                          # 抓 1 帧(默认 index 0)
  python3 camera_capture.py --list                   # 用 system_profiler 枚举摄像头
  python3 camera_capture.py --index 1                # 按序号选相机
  python3 camera_capture.py --name UGREEN            # 按名字子串选相机
  python3 camera_capture.py --frames 3 --interval 1  # 多帧 + 帧间隔(秒)
  python3 camera_capture.py --outdir /tmp/frames     # 自定义输出目录
  python3 camera_capture.py --allow-no-camera        # 无相机时快速 exit 2 不挂起

约定(镜像 tools/serial_capture.py)：
  - 状态信息 [OK/WARN/ERR/SKIP][cam] 打到 stderr；数据(NDJSON / 裸 index)打到 stdout。
  - `--list` 无相机 → stderr [SKIP][cam] + 空 stdout + exit 2。
  - 无可用帧(read 失败或连续黑帧)→ [ERR][cam] 授权提示 + exit 1。

依赖：pip3 install opencv-contrib-python numpy
"""

import argparse
import hashlib
import json
import signal
import subprocess
import sys
import time
from datetime import datetime
from pathlib import Path

try:
    import cv2
except ImportError:
    print("[ERR][cam] opencv-contrib-python not installed. "
          "Run: pip3 install opencv-contrib-python numpy", file=sys.stderr)
    sys.exit(1)

# 抓帧参数
FRAME_WIDTH = 1280
FRAME_HEIGHT = 720
JPEG_QUALITY = 90
WARMUP_FRAMES = 5          # 丢弃前 N 帧让 AGC/AWB 收敛
BLACK_THRESHOLD = 3.0      # 平均亮度低于此值视为黑帧
BLACK_LIMIT = 5            # 连续 N 次黑帧/读取失败判定无可用帧


def enumerate_cameras():
    """用 system_profiler 枚举摄像头，返回名字列表(顺序即 index)。

    观测到的真实 JSON 结构:
      {"SPCameraDataType": [{"_name": "...", "spcamera_model-id": "...", ...}, ...]}
    """
    try:
        out = subprocess.run(
            ["system_profiler", "SPCameraDataType", "-json"],
            capture_output=True, text=True, timeout=15,
        )
    except (subprocess.TimeoutExpired, FileNotFoundError, OSError) as e:
        print("[ERR][cam] system_profiler failed: %s" % e, file=sys.stderr)
        return []

    if out.returncode != 0:
        print("[ERR][cam] system_profiler exit %d: %s"
              % (out.returncode, (out.stderr or "").strip()), file=sys.stderr)
        return []

    try:
        data = json.loads(out.stdout or "{}")
    except json.JSONDecodeError as e:
        print("[ERR][cam] cannot parse system_profiler JSON: %s" % e,
              file=sys.stderr)
        return []

    cams = data.get("SPCameraDataType", []) or []
    return [c.get("_name", "<unnamed>") for c in cams]


def list_cameras():
    """列出摄像头：stderr 打概览+明细，stdout 打裸 index(供 shell 循环消费)。

    无相机 → [SKIP][cam] + 空 stdout + exit 2(镜像 serial_capture 列举语义)。
    """
    names = enumerate_cameras()
    if not names:
        print("[SKIP][cam] no cameras detected", file=sys.stderr)
        sys.exit(2)

    print("[OK][cam] found %d camera(s):" % len(names), file=sys.stderr)
    for i, name in enumerate(names):
        print("  %d — %s" % (i, name), file=sys.stderr)
    # 裸 index 打到 stdout，供 shell 循环消费
    for i in range(len(names)):
        print(i)
    sys.exit(0)


def resolve_index(index_arg, name_arg):
    """确定要打开的相机 index。--name 优先在 system_profiler 名字里匹配子串。

    返回 (index, names_list)。若 --name 无匹配 → 报错 exit 1。
    """
    names = enumerate_cameras()
    if name_arg is not None:
        matches = [i for i, n in enumerate(names)
                   if name_arg.lower() in (n or "").lower()]
        if not matches:
            print("[ERR][cam] no camera name contains %r; available: %s"
                  % (name_arg, names or "(none)"), file=sys.stderr)
            sys.exit(1)
        return matches[0], names
    return index_arg, names


def _sha256_file(path):
    h = hashlib.sha256()
    with open(path, "rb") as f:
        for chunk in iter(lambda: f.read(65536), b""):
            h.update(chunk)
    return h.hexdigest()


def _grab_usable_frame(cap):
    """连续读取直到拿到一帧非黑画面，或连续 BLACK_LIMIT 次坏读则放弃。

    返回帧(numpy 数组)或 None。None 代表 read 失败或持续黑帧(可能是 TCC 未授权)。
    """
    streak = 0
    while streak < BLACK_LIMIT:
        ok, frame = cap.read()
        if not ok or frame is None:
            streak += 1
            continue
        if float(frame.mean()) < BLACK_THRESHOLD:
            streak += 1
            continue
        return frame
    return None


class _CaptureTimeout(Exception):
    """整体超时(SIGALRM)触发时抛出。"""


def _install_timeout(timeout):
    """用 SIGALRM 装一个整体超时。AVFoundation 必须跑在主线程，故不用工作线程，
    改用信号做超时(SIGALRM 只能在主线程注册，正合此处)。返回是否成功装上。
    """
    if timeout and timeout > 0 and hasattr(signal, "SIGALRM"):
        def _on_alarm(_signum, _frame):
            raise _CaptureTimeout()
        signal.signal(signal.SIGALRM, _on_alarm)
        signal.alarm(int(timeout))
        return True
    return False


def capture(index, frames, interval, outdir, timeout):
    """在主线程执行 open + warm-up + 抓帧 + 存盘，NDJSON 打到 stdout。

    AVFoundation 后端要求在主线程运行(否则报 'can not spin main run loop
    from other thread')，因此这里不开工作线程，而用 SIGALRM 做整体超时。

    退出码：成功 0；打开失败/无可用帧/超时 1；(无相机在 main() 里已 exit 2)。
    """
    print("[OK][cam] capturing %d frame(s) from index %d (timeout=%ds)"
          % (frames, index, timeout), file=sys.stderr)

    armed = _install_timeout(timeout)
    cap = None
    try:
        cap = cv2.VideoCapture(index, cv2.CAP_AVFOUNDATION)
        if not cap.isOpened():
            # macOS 上 TCC 未授权时 AVFoundation 直接打开失败，故这里给出授权提示。
            print("[ERR][cam] cannot open camera index %d — 相机不存在、被占用，"
                  "或未授权。检查 系统设置→隐私与安全→相机 是否已授权给当前终端"
                  % index, file=sys.stderr)
            sys.exit(1)

        cap.set(cv2.CAP_PROP_FRAME_WIDTH, FRAME_WIDTH)
        cap.set(cv2.CAP_PROP_FRAME_HEIGHT, FRAME_HEIGHT)

        # warm-up：丢弃前若干帧，让曝光/白平衡(AGC/AWB)收敛
        for _ in range(WARMUP_FRAMES):
            cap.read()

        outdir.mkdir(parents=True, exist_ok=True)
        for i in range(frames):
            if i > 0 and interval > 0:
                time.sleep(interval)
            frame = _grab_usable_frame(cap)
            if frame is None:
                # read 持续失败或连续黑帧 —— 绝不静默产出黑帧 NDJSON。
                print("[ERR][cam] no usable frames — 检查 系统设置→隐私与安全"
                      "→相机 是否已授权给当前终端", file=sys.stderr)
                sys.exit(1)
            now = datetime.now()
            ts = now.isoformat()
            fname = "%d-%s.jpg" % (i, now.strftime("%Y%m%d-%H%M%S-%f"))
            fpath = outdir / fname
            cv2.imwrite(str(fpath), frame,
                        [cv2.IMWRITE_JPEG_QUALITY, JPEG_QUALITY])
            h, w = frame.shape[0], frame.shape[1]
            # 每帧一行 NDJSON 打到 stdout(逐帧 flush，多帧时不丢)
            print(json.dumps({
                "path": str(fpath),
                "sha256": _sha256_file(str(fpath)),
                "index": index,
                "ts": ts,
                "w": w,
                "h": h,
            }, ensure_ascii=False), flush=True)
    except _CaptureTimeout:
        print("[ERR][cam] timeout after %ds while opening/capturing camera "
              "index %d" % (timeout, index), file=sys.stderr)
        sys.exit(1)
    finally:
        if armed:
            signal.alarm(0)
        if cap is not None:
            cap.release()

    print("[OK][cam] capture complete — %d frame(s) saved"
          % frames, file=sys.stderr)
    sys.exit(0)


def main():
    parser = argparse.ArgumentParser(
        description="macOS Camera Capture Tool — pure frame capture "
                    "(no perception, no network)"
    )
    parser.add_argument("--list", "-l", action="store_true",
                        help="List cameras via system_profiler and exit")
    parser.add_argument("--index", "-i", type=int, default=0,
                        help="Camera index to open (default: 0)")
    parser.add_argument("--name", "-n", default=None,
                        help="Select camera by name substring "
                             "(matched against system_profiler names)")
    parser.add_argument("--frames", "-f", type=int, default=1,
                        help="Number of frames to capture (default: 1)")
    parser.add_argument("--interval", type=float, default=0.0,
                        help="Delay in seconds between frames (default: 0)")
    parser.add_argument("--timeout", "-t", type=int, default=15,
                        help="Overall open+capture timeout in seconds "
                             "(default: 15)")
    parser.add_argument("--outdir", "-o", default=None,
                        help="Output directory "
                             "(default: .omo/evidence/frames/<runid>/)")
    parser.add_argument("--allow-no-camera", action="store_true",
                        help="If no camera is present, exit 2 immediately "
                             "instead of hanging")

    args = parser.parse_args()

    if args.list:
        list_cameras()
        return

    # --allow-no-camera：抓帧前先枚举，无相机则立即 exit 2 不挂起
    if args.allow_no_camera:
        if not enumerate_cameras():
            print("[SKIP][cam] no cameras detected", file=sys.stderr)
            sys.exit(2)

    index, names = resolve_index(args.index, args.name)

    # 若系统一台相机都没有 → 采集也走 [SKIP] + exit 2(headless 语义)
    if not names:
        print("[SKIP][cam] no cameras detected", file=sys.stderr)
        sys.exit(2)

    runid = time.strftime("%Y%m%d-%H%M%S")
    if args.outdir:
        outdir = Path(args.outdir)
    else:
        outdir = Path(".omo/evidence/frames") / runid

    capture(index, args.frames, args.interval, outdir, args.timeout)


if __name__ == "__main__":
    main()
