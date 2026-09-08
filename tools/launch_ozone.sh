#!/usr/bin/env bash
#
# RoboMaster C 板（rm_dev_board_c）Ozone 调试一键入口
#
# 做三件事，顺序固定：
#   1) 用 arm-none-eabi 工具链配置并编译 CMAKE_BUILD_TYPE=Debug（-O0 -g3）
#   2) 编译成功后，才把无扩展名的 ELF 复制成 rm_c_blinky.elf
#   3) 校验 Ozone 可执行文件与仓库内 .jdebug 工程存在，然后打开工程
#
# 用法：
#   bash tools/launch_ozone.sh              # 编译 + 打开 Ozone
#   bash tools/launch_ozone.sh --dry-run    # 只校验路径与前置条件，不编译、不启动
#   bash tools/launch_ozone.sh --build-only # 编译并产出 .elf，但不启动 Ozone
#
# 说明：
#   - 不修改 PATH、不写任何机器级配置，Ozone 路径只在本进程内解析。
#   - 不烧录。烧录仍由 tools/build_and_flash.sh 负责。
#   - 缺少 Ozone 或缺少 .jdebug 工程时，以固定退出码失败（见下方 EXIT_* 常量）。
#   - ELF 不是"存在即通过"：还要用 arm-none-eabi-readelf/nm 验证它确实是
#     ARM ELF32 可执行文件、带非空 .debug_info、并含 g_gyro_snapshot 符号，
#     且与当前源码/构建产物一致（不过期）。
#
set -euo pipefail

# ------------------------------------------------------------------
# 固定退出码：便于脚本化判断失败原因
# ------------------------------------------------------------------
readonly EXIT_USAGE=2
readonly EXIT_NO_OZONE=3
readonly EXIT_NO_PROJECT=4
readonly EXIT_NO_ELF=5
readonly EXIT_BUILD_FAILED=6
readonly EXIT_BAD_ELF=7
readonly EXIT_STALE_ELF=8
readonly EXIT_NO_ARM_TOOLS=9

# ------------------------------------------------------------------
# 从脚本自身位置定位仓库根，不依赖调用方的当前目录
# ------------------------------------------------------------------
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
readonly SCRIPT_DIR
REPO_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
readonly REPO_ROOT

# ------------------------------------------------------------------
# 本工作流的规范常量（canonical，勿随手改名）
# ------------------------------------------------------------------
readonly TARGET_BOARD=rm_dev_board_c
readonly APP_NAME=rm_c_blinky
readonly BUILD_DIR="$REPO_ROOT/build-${TARGET_BOARD}-debug"
readonly TOOLCHAIN_FILE="$REPO_ROOT/cmake/toolchain/arm-none-eabi-gcc.cmake"
readonly APP_BUILD_DIR="$BUILD_DIR/apps/$APP_NAME"
readonly ELF_RAW="$APP_BUILD_DIR/$APP_NAME"
readonly ELF_PATH="$APP_BUILD_DIR/$APP_NAME.elf"
readonly OZONE_PROJECT="$REPO_ROOT/apps/$APP_NAME/ozone/$APP_NAME.jdebug"

# Ozone 安装位置候选。/Applications/SEGGER/Ozone 是版本符号链接
# （当前 -> Ozone_V350a），带版本号的路径作为符号链接缺失时的兜底。
readonly OZONE_CANDIDATES=(
    "/Applications/SEGGER/Ozone/Ozone.app/Contents/MacOS/Ozone"
    "/Applications/SEGGER/Ozone_V350a/Ozone.app/Contents/MacOS/Ozone"
)

DRY_RUN=false
BUILD_ONLY=false

log()  { printf '[INFO] %s\n' "$*"; }
ok()   { printf '[OK] %s\n' "$*"; }
err()  { printf '[ERR] %s\n' "$*" >&2; }

usage() {
    cat <<EOF
用法: bash tools/launch_ozone.sh [--dry-run|--build-only]

  --dry-run     校验路径与 ELF 内容（ARM ELF32 头、非空 .debug_info、
                g_gyro_snapshot 符号、是否过期），不配置、不编译、不启动 Ozone。
  --build-only  执行 Debug 构建、产出并校验 .elf，但不启动 Ozone。
  -h, --help    显示本帮助。

退出码: $EXIT_USAGE=参数错误 $EXIT_NO_OZONE=未找到 Ozone $EXIT_NO_PROJECT=未找到 .jdebug
        $EXIT_NO_ELF=未找到 ELF $EXIT_BUILD_FAILED=构建失败
        $EXIT_BAD_ELF=ELF 无效/缺调试信息 $EXIT_STALE_ELF=ELF 过期
        $EXIT_NO_ARM_TOOLS=缺少 arm-none-eabi 校验工具
EOF
}

while [ "$#" -gt 0 ]; do
    case "$1" in
        --dry-run)    DRY_RUN=true ;;
        --build-only) BUILD_ONLY=true ;;
        -h|--help)    usage; exit 0 ;;
        *)
            err "unknown arg: $1"
            usage >&2
            exit "$EXIT_USAGE"
            ;;
    esac
    shift
done

# ------------------------------------------------------------------
# 解析 Ozone 可执行文件。找不到时返回非零，由调用点决定是硬失败
# 还是仅作为 dry-run 的一条报告。
#
# OZONE_BIN 可由环境变量覆盖（仅对本次调用生效，不写任何持久配置）：
# 既用于非标准安装位置，也用于测试「未安装 Ozone」这条失败分支。
# ------------------------------------------------------------------
OZONE_BIN_OVERRIDE="${OZONE_BIN:-}"
readonly OZONE_BIN_OVERRIDE
OZONE_BIN=""
resolve_ozone() {
    local candidate
    if [ -n "$OZONE_BIN_OVERRIDE" ]; then
        if [ -x "$OZONE_BIN_OVERRIDE" ]; then
            OZONE_BIN="$OZONE_BIN_OVERRIDE"
            return 0
        fi
        return 1
    fi
    for candidate in "${OZONE_CANDIDATES[@]}"; do
        if [ -x "$candidate" ]; then
            OZONE_BIN="$candidate"
            return 0
        fi
    done
    return 1
}

report_missing_ozone() {
    local candidate
    if [ -n "$OZONE_BIN_OVERRIDE" ]; then
        err "OZONE_BIN 指向的文件不可执行: $OZONE_BIN_OVERRIDE"
        return
    fi
    err "未找到 Ozone 可执行文件，已尝试:"
    for candidate in "${OZONE_CANDIDATES[@]}"; do
        err "  $candidate"
    done
}

print_paths() {
    printf 'repo root      : %s\n' "$REPO_ROOT"
    printf 'target board   : %s\n' "$TARGET_BOARD"
    printf 'build type     : Debug\n'
    printf 'build dir      : %s\n' "$BUILD_DIR"
    printf 'toolchain file : %s\n' "$TOOLCHAIN_FILE"
    printf 'elf (raw)      : %s\n' "$ELF_RAW"
    printf 'elf (.elf)     : %s\n' "$ELF_PATH"
    printf 'ozone project  : %s\n' "$OZONE_PROJECT"
    printf 'ozone binary   : %s\n' "${OZONE_BIN:-<not found>}"
}

# ------------------------------------------------------------------
# ELF 校验所需的 arm-none-eabi 工具。与构建用的是同一套工具链。
# ------------------------------------------------------------------
readonly READELF_BIN="${READELF:-arm-none-eabi-readelf}"
readonly NM_BIN="${NM:-arm-none-eabi-nm}"

require_arm_tools() {
    local missing=false tool
    for tool in "$READELF_BIN" "$NM_BIN"; do
        if ! command -v "$tool" >/dev/null 2>&1; then
            err "未找到 ELF 校验工具: $tool"
            missing=true
        fi
    done
    if [ "$missing" = true ]; then
        err "请安装 arm-none-eabi 工具链（brew install arm-gcc-bin）。"
        return 1
    fi
    return 0
}

# ------------------------------------------------------------------
# 检查一个文件是否为 Ozone 可用的调试镜像。逐项独立判定并全部打印，
# 便于一次看清所有问题；返回第一个失败对应的退出码。
#
# 之所以不能只用 [ -f ]：空文件、被截断的文件、objcopy 出来的 .bin、
# 以及 Release 构建的无 DWARF ELF 都能通过存在性检查，但在 Ozone 里
# 表现为"能加载、看不到变量"，属于最难排查的一类失败。
# ------------------------------------------------------------------
validate_elf() {
    local elf="$1"
    local status=0
    local size header class machine type debug_size

    if [ ! -f "$elf" ]; then
        err "ELF 不存在: $elf"
        return "$EXIT_NO_ELF"
    fi

    size=$(wc -c < "$elf" | tr -d ' ')
    if [ "$size" -eq 0 ]; then
        err "ELF 为空文件（0 字节）: $elf"
        return "$EXIT_BAD_ELF"
    fi

    # readelf -h 对非 ELF / 头部损坏的输入返回非零，这是"有效头"的判据。
    if ! header=$("$READELF_BIN" -h "$elf" 2>&1); then
        err "ELF 头无效（不是 ELF 或已损坏）: $elf"
        err "  $READELF_BIN -h 输出: $(printf '%s' "$header" | head -1)"
        return "$EXIT_BAD_ELF"
    fi

    class=$(printf '%s\n' "$header"  | awk -F: '/^  Class:/            {gsub(/ /,"",$2); print $2}')
    machine=$(printf '%s\n' "$header"| awk -F: '/^  Machine:/          {sub(/^ +/,"",$2); print $2}')
    type=$(printf '%s\n' "$header"   | awk -F: '/^  Type:/             {sub(/^ +/,"",$2); print $2}')

    if [ "$class" != "ELF32" ]; then
        err "ELF class 期望 ELF32，实际: ${class:-<unknown>}"
        status="$EXIT_BAD_ELF"
    fi
    if [ "$machine" != "ARM" ]; then
        err "ELF machine 期望 ARM，实际: ${machine:-<unknown>}"
        status="$EXIT_BAD_ELF"
    fi
    case "$type" in
        EXEC*) : ;;
        *)
            err "ELF type 期望 EXEC (可执行文件)，实际: ${type:-<unknown>}"
            status="$EXIT_BAD_ELF"
            ;;
    esac
    if [ "$status" -eq 0 ]; then
        ok "ELF 头有效: $class / $machine / $type ($size bytes)"
    fi

    # readelf -S 把节索引右对齐成固定宽度，所以单位数索引 "[ 9]" 会被
    # 空格切成两个字段（"[" 和 "9]"），双位数 "[13]" 只占一个字段。写死
    # $2/$6 只在双位数索引下正确；.debug_info 落到单位数索引时会误判成
    # "缺少 .debug_info"。这里先定位节名所在字段，再按相对位置取 Size。
    # 字段顺序（节名之后）: Type Addr Off Size
    debug_size=$("$READELF_BIN" -SW "$elf" 2>/dev/null | awk '
        {
            for (i = 1; i <= NF; i++) {
                if ($i == ".debug_info") { print $(i + 4); exit }
            }
        }')
    if [ -z "$debug_size" ]; then
        err "ELF 缺少 .debug_info 节（是否用了 Release 构建或已 strip？）"
        [ "$status" -eq 0 ] && status="$EXIT_BAD_ELF"
    elif [ "$((16#$debug_size))" -eq 0 ]; then
        err ".debug_info 节为空（大小 0）"
        [ "$status" -eq 0 ] && status="$EXIT_BAD_ELF"
    else
        ok ".debug_info 非空: 0x$debug_size ($((16#$debug_size)) bytes)"
    fi

    if "$NM_BIN" "$elf" 2>/dev/null | grep -qE ' [BbDd] g_gyro_snapshot$'; then
        ok "符号 g_gyro_snapshot 存在: $("$NM_BIN" "$elf" | grep ' g_gyro_snapshot$' | head -1)"
    else
        err "ELF 中未找到 g_gyro_snapshot 数据符号（Ozone 将无法观察快照）"
        [ "$status" -eq 0 ] && status="$EXIT_BAD_ELF"
    fi

    return "$status"
}

mtime_of() {
    stat -f %m "$1" 2>/dev/null || echo 0
}

# ------------------------------------------------------------------
# 过期检测，两条独立判据：
#   1) .elf 必须与同目录下无扩展名的构建产物逐字节一致。不一致说明
#      .elf 是上一次构建留下的副本，而 Ninja 已重新链接过。
#   2) .elf 必须比所有相关源码/CMake 输入更新。
# ------------------------------------------------------------------
# middleware/ 必须在列表里：FreeRTOS-Kernel 是链进本固件的源码内嵌内核，
# 改动它（含 port.c / heap_4.c）会改变 ELF，却不在其他任何目录之下。
readonly STALENESS_INPUTS=(
    "$REPO_ROOT/CMakeLists.txt"
    "$REPO_ROOT/cmake"
    "$REPO_ROOT/apps/$APP_NAME"
    "$REPO_ROOT/boards/$TARGET_BOARD"
    "$REPO_ROOT/drivers"
    "$REPO_ROOT/bsp/stm32/f4"
    "$REPO_ROOT/middleware"
)

validate_elf_freshness() {
    local elf="$1"
    local status=0
    local newer_count input changed

    if [ ! -f "$ELF_RAW" ]; then
        err "构建产物缺失: ${ELF_RAW}（无法判定 $elf 是否过期）"
        return "$EXIT_STALE_ELF"
    fi
    if ! cmp -s "$ELF_RAW" "$elf"; then
        err ".elf 与构建产物不一致（.elf 已过期）:"
        err "  raw : $ELF_RAW ($(wc -c < "$ELF_RAW" | tr -d ' ') bytes, mtime $(mtime_of "$ELF_RAW"))"
        err "  copy: $elf ($(wc -c < "$elf" | tr -d ' ') bytes, mtime $(mtime_of "$elf"))"
        status="$EXIT_STALE_ELF"
    else
        ok ".elf 与构建产物逐字节一致"
    fi

    # 用 -newer <参照文件> 而不是 -newermt "@epoch"：BSD find 不接受 @epoch
    # 形式，且整秒精度会漏掉同一秒内发生的编辑。-newer 按完整时间戳比较。
    newer_count=0
    for input in "${STALENESS_INPUTS[@]}"; do
        [ -e "$input" ] || continue
        while IFS= read -r changed; do
            [ -n "$changed" ] || continue
            err "源码比 .elf 更新: ${changed#"$REPO_ROOT"/}"
            newer_count=$((newer_count + 1))
        done < <(find "$input" \
                    \( -name '*.c' -o -name '*.h' -o -name '*.s' -o -name '*.S' \
                       -o -name '*.ld' -o -name '*.cmake' -o -name 'CMakeLists.txt' \) \
                    -newer "$elf" -print 2>/dev/null)
    done
    if [ "$newer_count" -gt 0 ]; then
        err "共 $newer_count 个输入文件比 .elf 更新 -> 需要重新构建"
        [ "$status" -eq 0 ] && status="$EXIT_STALE_ELF"
    else
        ok "没有源码/CMake 输入比 .elf 更新"
    fi

    return "$status"
}

# ==================================================================
# --dry-run：只校验前置条件
# ==================================================================
if [ "$DRY_RUN" = true ]; then
    log "dry-run: 校验路径，不构建、不启动 Ozone"
    resolve_ozone || true
    print_paths

    # 检查全部跑完以便一次暴露所有缺口，但退出码固定取第一个失败：
    # 「缺 Ozone」必须恒为 EXIT_NO_OZONE，不被后续检查覆盖。
    dry_run_status=0
    record_failure() {
        if [ "$dry_run_status" -eq 0 ]; then
            dry_run_status="$1"
        fi
    }
    record_failure_if() {
        if [ "$1" -ne 0 ]; then
            record_failure "$1"
        fi
    }

    if [ ! -f "$TOOLCHAIN_FILE" ]; then
        err "toolchain file 缺失: $TOOLCHAIN_FILE"
        record_failure "$EXIT_USAGE"
    else
        ok "toolchain file 存在"
    fi

    if [ -z "$OZONE_BIN" ]; then
        report_missing_ozone
        record_failure "$EXIT_NO_OZONE"
    else
        ok "Ozone 可执行文件存在: $OZONE_BIN"
    fi

    if [ ! -f "$OZONE_PROJECT" ]; then
        err "未找到仓库内 Ozone 工程: $OZONE_PROJECT"
        record_failure "$EXIT_NO_PROJECT"
    else
        ok "Ozone 工程存在: $OZONE_PROJECT"
    fi

    if ! require_arm_tools; then
        record_failure "$EXIT_NO_ARM_TOOLS"
    else
        ok "ELF 校验工具就绪: $READELF_BIN, $NM_BIN"

        if [ ! -f "$ELF_PATH" ]; then
            err "Debug ELF 尚不存在: ${ELF_PATH}（去掉 --dry-run 即可构建）"
            record_failure "$EXIT_NO_ELF"
        else
            log "校验 Debug ELF 内容: $ELF_PATH"
            elf_status=0
            validate_elf "$ELF_PATH" || elf_status=$?
            record_failure_if "$elf_status"

            # ELF 本身已经无效时不再报"过期"：坏文件几乎必然同时与构建产物
            # 不一致，两条一起报只会混淆首要诊断。两种情况的处置都是重新构建。
            if [ "$elf_status" -ne 0 ]; then
                log "跳过过期检测：ELF 内容校验已失败，请先重新构建"
            else
                fresh_status=0
                validate_elf_freshness "$ELF_PATH" || fresh_status=$?
                record_failure_if "$fresh_status"
            fi
        fi
    fi

    if [ "$dry_run_status" -ne 0 ]; then
        err "dry-run 失败，退出码 $dry_run_status"
        exit "$dry_run_status"
    fi
    ok "dry-run 通过：构建与启动的全部前置条件已满足"
    exit 0
fi

# ==================================================================
# Step 1: 配置 + 构建 Debug
# ==================================================================
log "配置 CMake（Ninja，Debug，${TARGET_BOARD}）-> $BUILD_DIR"
if ! cmake -S "$REPO_ROOT" -B "$BUILD_DIR" -G Ninja \
        -DCMAKE_TOOLCHAIN_FILE="$TOOLCHAIN_FILE" \
        -DTARGET_BOARD="$TARGET_BOARD" \
        -DCMAKE_BUILD_TYPE=Debug; then
    err "CMake 配置失败"
    exit "$EXIT_BUILD_FAILED"
fi

log "编译 ${APP_NAME}（Debug）"
if ! cmake --build "$BUILD_DIR"; then
    err "构建失败"
    exit "$EXIT_BUILD_FAILED"
fi
ok "构建成功"

# ------------------------------------------------------------------
# Step 2: 仅在构建成功后复制 ELF。CMake 产出的可执行文件没有扩展名，
# Ozone 工程按 .elf 引用，所以这里显式复制而不是改动构建目标。
# ------------------------------------------------------------------
if [ ! -f "$ELF_RAW" ]; then
    err "未找到构建产物: $ELF_RAW"
    exit "$EXIT_NO_ELF"
fi
cp -f "$ELF_RAW" "$ELF_PATH"
ok "Debug ELF 就绪: $ELF_PATH ($(wc -c < "$ELF_PATH" | tr -d ' ') bytes)"

# 刚复制出来的 ELF 也要过同一套校验：构建成功但产物不含 DWARF
# （例如有人改了编译类型）必须在启动 Ozone 之前就失败。
if ! require_arm_tools; then
    exit "$EXIT_NO_ARM_TOOLS"
fi
elf_status=0
validate_elf "$ELF_PATH" || elf_status=$?
if [ "$elf_status" -ne 0 ]; then
    err "构建产物未通过 ELF 校验，退出码 $elf_status"
    exit "$elf_status"
fi

if [ "$BUILD_ONLY" = true ]; then
    log "--build-only: 跳过 Ozone 启动"
    resolve_ozone || true
    print_paths
    exit 0
fi

# ==================================================================
# Step 3: 校验 Ozone 与工程，然后打开
# ==================================================================
if ! resolve_ozone; then
    report_missing_ozone
    err "请安装 SEGGER Ozone V3.50a，或修正 /Applications/SEGGER/Ozone 符号链接。"
    exit "$EXIT_NO_OZONE"
fi
ok "Ozone: $OZONE_BIN"

if [ ! -f "$OZONE_PROJECT" ]; then
    err "未找到仓库内 Ozone 工程: $OZONE_PROJECT"
    exit "$EXIT_NO_PROJECT"
fi
ok "Ozone 工程: $OZONE_PROJECT"

log "启动 Ozone..."
exec "$OZONE_BIN" -project "$OZONE_PROJECT"
