#!/usr/bin/env bash
#
# STM32 一键编译 + 烧录脚本
#
# 用法：
#   bash build_and_flash.sh              # 编译并烧录
#   bash build_and_flash.sh --no-flash   # 只编译，不烧录
#
# 依赖：
#   - cmake + ninja（构建工具）
#   - JLinkExe（J-Link 烧录工具）
#   - 已连接 J-Link 和 STM32 板（通电）
#
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_DIR="$(dirname "$SCRIPT_DIR")"
BUILD_DIR="$PROJECT_DIR/build"

NO_FLASH=false

# 参数解析
for arg in "$@"; do
    case "$arg" in
        --no-flash) NO_FLASH=true ;;
        *) echo "[WARN] unknown arg: $arg" ;;
    esac
done

# ============================================================
# Step 1: 配置（如果需要）+ 编译
# ============================================================
echo "[INFO] Building firmware..."
cd "$PROJECT_DIR"

# 如果 build 目录未配置，先配置
if [ ! -f "$BUILD_DIR/build.ninja" ]; then
    echo "[INFO] Configuring CMake..."
    cmake -B build -G Ninja -DCMAKE_TOOLCHAIN_FILE=cmake/toolchain/arm-none-eabi-gcc.cmake
fi

cmake --build "$BUILD_DIR"
echo "[OK] Build succeeded"

# 确认产物
BIN_FILE="$BUILD_DIR/apps/blinky_f103/blinky_f103.bin"
if [ ! -f "$BIN_FILE" ]; then
    echo "[ERR] Binary not found: $BIN_FILE"
    exit 1
fi
echo "[OK] Binary ready: $BIN_FILE ($(wc -c < "$BIN_FILE") bytes)"

# ============================================================
# Step 2: 烧录（可选）
# ============================================================
if [ "$NO_FLASH" = true ]; then
    echo "[INFO] --no-flash: skipping flash step"
    exit 0
fi

# 检查 JLinkExe
JLINK="$(which JLinkExe 2>/dev/null || true)"
if [ -z "$JLINK" ]; then
    echo "[ERR] JLinkExe not found. Install J-Link software."
    exit 1
fi
echo "[INFO] Using J-Link: $JLINK"

# 检查 flash 脚本
FLASH_SCRIPT="$PROJECT_DIR/apps/blinky_f103/flash.jlink"
if [ ! -f "$FLASH_SCRIPT" ]; then
    echo "[ERR] Flash script not found: $FLASH_SCRIPT"
    exit 1
fi

echo "[INFO] Flashing firmware to STM32F103C8..."

# 运行 J-Link 烧录
if ! "$JLINK" -device STM32F103C8 -if SWD -speed 4000 -autoconnect 1 \
              -CommandFile "$FLASH_SCRIPT" 2>&1; then
    echo "[ERR] J-Link flash failed"
    echo "[ERR] Check: 1) J-Link connected? 2) Board powered? 3) BOOT0=GND?"
    exit 1
fi

# 等待 MCU 启动
sleep 2

echo "FLASH OK"
