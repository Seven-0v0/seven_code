#!/usr/bin/env python3
"""
STM32 Serial Capture Tool — AI 可调用的串口数据捕获脚本

用途：打开指定串口，读取数据直到超时，将全部内容输出到 stdout。
      AI 通过解析 stdout 来获取 STM32 的调试日志。

用法：
  python3 serial_capture.py                          # 自动检测 /dev/tty.usbmodem*
  python3 serial_capture.py --device /dev/tty.xxx    # 手动指定设备
  python3 serial_capture.py --baud 9600              # 指定波特率（默认 115200）
  python3 serial_capture.py --timeout 15             # 捕获超时秒数（默认 10）
  python3 serial_capture.py --list                   # 列出所有可用串口

依赖：pip3 install pyserial
"""

import argparse
import glob
import sys
import time

try:
    import serial
    import serial.tools.list_ports
except ImportError:
    print("[ERR][serial] pyserial not installed. Run: pip3 install pyserial",
          file=sys.stderr)
    sys.exit(1)


def list_ports():
    """列出所有可用串口设备"""
    ports = serial.tools.list_ports.comports()
    if not ports:
        print("[WARN][serial] no serial ports detected", file=sys.stderr)
        sys.exit(1)

    print("[OK][serial] found %d port(s):" % len(ports), file=sys.stderr)
    for p in ports:
        print("  %s — %s (VID:PID=%04X:%04X)" %
              (p.device, p.description, p.vid or 0, p.pid or 0),
              file=sys.stderr)
    # Also print bare device paths for scripting
    for p in ports:
        print(p.device)


def auto_detect():
    """自动检测 USB 串口设备"""
    patterns = [
        '/dev/tty.usbmodem*',
        '/dev/tty.usbserial*',
        '/dev/tty.wchusbserial*',
        '/dev/tty.SLAB_USBtoUART*',
    ]
    for pat in patterns:
        matches = glob.glob(pat)
        if matches:
            return matches[0]
    return None


def capture(device, baud, timeout):
    """打开串口并捕获数据"""
    try:
        ser = serial.Serial(device, baudrate=baud, timeout=1.0)
    except (serial.SerialException, OSError) as e:
        print("[ERR][serial] cannot open %s: %s" % (device, e), file=sys.stderr)
        sys.exit(1)

    print("[OK][serial] capturing from %s @ %d baud (timeout=%ds)" %
          (device, baud, timeout), file=sys.stderr)

    deadline = time.time() + timeout
    while time.time() < deadline:
        if ser.in_waiting:
            data = ser.read(ser.in_waiting)
            sys.stdout.buffer.write(data)
            sys.stdout.buffer.flush()
        else:
            time.sleep(0.05)

    # 最后清空缓冲区
    remaining = ser.in_waiting
    if remaining:
        data = ser.read(remaining)
        sys.stdout.buffer.write(data)
        sys.stdout.buffer.flush()

    ser.close()
    print("\n[OK][serial] capture complete", file=sys.stderr)


def main():
    parser = argparse.ArgumentParser(
        description="STM32 Serial Capture Tool — capture UART debug output"
    )
    parser.add_argument("--device", "-d", default=None,
                        help="Serial device path (auto-detect if omitted)")
    parser.add_argument("--baud", "-b", type=int, default=115200,
                        help="Baud rate (default: 115200)")
    parser.add_argument("--timeout", "-t", type=int, default=10,
                        help="Capture timeout in seconds (default: 10)")
    parser.add_argument("--list", "-l", action="store_true",
                        help="List available serial ports and exit")

    args = parser.parse_args()

    if args.list:
        list_ports()
        sys.exit(0)

    device = args.device or auto_detect()
    if not device:
        print("[ERR][serial] no device found. Use --device or --list",
              file=sys.stderr)
        sys.exit(1)

    capture(device, args.baud, args.timeout)


if __name__ == "__main__":
    main()
