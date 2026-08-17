#!/usr/bin/env bash
#
# STM32 一键编译 + 烧录脚本
#
# 用法：
#   bash build_and_flash.sh                       # 默认 f103：编译并烧录
#   bash build_and_flash.sh --no-flash            # 默认 f103：只编译
#   bash build_and_flash.sh --target rm_dev_board_c --no-flash
#
# 依赖：
#   - cmake + ninja（构建工具）
#   - JLinkExe（J-Link 烧录工具）
#   - 已连接 J-Link 和 STM32 板（通电）
#
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_DIR="$(dirname "$SCRIPT_DIR")"
NO_FLASH=false
TARGET_BOARD=f103

# 参数解析
while [ "$#" -gt 0 ]; do
    case "$1" in
        --no-flash) NO_FLASH=true ;;
        --target=f103) TARGET_BOARD=f103 ;;
        --target=rm_dev_board_c) TARGET_BOARD=rm_dev_board_c ;;
        --target)
            if [ "$#" -lt 2 ]; then
                echo "[ERR] --target requires f103 or rm_dev_board_c"
                exit 2
            fi
            TARGET_BOARD="$2"
            shift
            ;;
        *)
            echo "[ERR] unknown arg: $1"
            exit 2
            ;;
    esac
    shift
done

case "$TARGET_BOARD" in
    f103)
        BUILD_DIR="$PROJECT_DIR/build"
        APP_NAME=blinky_f103
        JLINK_DEVICE=STM32F103C8
        ;;
    rm_dev_board_c)
        BUILD_DIR="$PROJECT_DIR/build-rm_dev_board_c"
        APP_NAME=rm_c_blinky
        JLINK_DEVICE=STM32F407IG
        ;;
    *)
        echo "[ERR] Unsupported target: $TARGET_BOARD (use f103 or rm_dev_board_c)"
        exit 2
        ;;
esac

# ============================================================
# Step 1: 配置（如果需要）+ 编译
# ============================================================
echo "[INFO] Building firmware for $TARGET_BOARD..."
cd "$PROJECT_DIR"

echo "[INFO] Configuring CMake..."
cmake -B "$BUILD_DIR" -G Ninja \
    -DCMAKE_TOOLCHAIN_FILE=cmake/toolchain/arm-none-eabi-gcc.cmake \
    -DTARGET_BOARD="$TARGET_BOARD"

cmake --build "$BUILD_DIR"
echo "[OK] Build succeeded"

# 确认产物
BIN_FILE="$BUILD_DIR/apps/$APP_NAME/$APP_NAME.bin"
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
FLASH_SCRIPT="$PROJECT_DIR/apps/$APP_NAME/flash.jlink"
if [ ! -f "$FLASH_SCRIPT" ]; then
    echo "[ERR] Flash script not found: $FLASH_SCRIPT"
    exit 1
fi

echo "[INFO] Flashing firmware to $JLINK_DEVICE..."

# 运行 J-Link 烧录
if ! "$JLINK" -device "$JLINK_DEVICE" -if SWD -speed 4000 -autoconnect 1 \
              -CommandFile "$FLASH_SCRIPT" 2>&1; then
    echo "[ERR] J-Link flash failed"
    echo "[ERR] Check: 1) J-Link connected? 2) Board powered? 3) BOOT0=GND?"
    exit 1
fi

# 等待 MCU 启动
sleep 2

echo "FLASH OK"
