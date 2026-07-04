cmake_minimum_required(VERSION 3.20)

# ============================================
# 芯片配置（在这里选择你的芯片）
# ============================================
set(MCU_VENDOR "stm32")      # 选择: "gd32" 或 "stm32"
set(MCU_SERIES "f4")        # 选择: "f0", "f1", "f3", "f4", "f7", "h7"
set(MCU_MODEL "GD32F407VE") # 具体型号（用于链接脚本和调试）

# ============================================
# 项目配置
# ============================================
project(GD32_STM32_Template C ASM)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

# ============================================
# MCU 编译选项
# ============================================
# Cortex-M4 (F4 系列)
if(MCU_SERIES STREQUAL "f4")
    set(MCU_FLAGS "-mcpu=cortex-m4 -mthumb -mfpu=fpv4-sp-d16 -mfloat-abi=hard")
# Cortex-M3 (F1, F3 部分型号)
elseif(MCU_SERIES STREQUAL "f1" OR MCU_SERIES STREQUAL "f3")
    set(MCU_FLAGS "-mcpu=cortex-m3 -mthumb")
# Cortex-M0 (F0 系列)
elseif(MCU_SERIES STREQUAL "f0")
    set(MCU_FLAGS "-mcpu=cortex-m0 -mthumb")
# Cortex-M7 (F7, H7 系列)
elseif(MCU_SERIES STREQUAL "f7" OR MCU_SERIES STREQUAL "h7")
    set(MCU_FLAGS "-mcpu=cortex-m7 -mthumb -mfpu=fpv5-d16 -mfloat-abi=hard")
endif()

set(CMAKE_C_FLAGS "${MCU_FLAGS} -Wall -Wextra -fdata-sections -ffunction-sections")
set(CMAKE_C_FLAGS_DEBUG "-O0 -g3")
set(CMAKE_C_FLAGS_RELEASE "-O2")

set(CMAKE_EXE_LINKER_FLAGS "${MCU_FLAGS} -specs=nano.specs -lc -lm -Wl,--gc-sections")

# ============================================
# BSP 路径配置
# ============================================
set(BSP_ROOT "/Users/seven.xu/code/bsp")

# 检查 BSP 是否存在
if(EXISTS ${BSP_ROOT}/${MCU_VENDOR}/${MCU_SERIES}/src)
    message(STATUS "使用 BSP: ${MCU_VENDOR}/${MCU_SERIES}")

    # 添加 BSP 库
    add_subdirectory(${BSP_ROOT}/${MCU_VENDOR}/${MCU_SERIES} bsp)

    set(BSP_AVAILABLE TRUE)
    set(BSP_TARGET bsp_${MCU_VENDOR}_${MCU_SERIES})
else()
    message(WARNING "BSP 库未找到: ${BSP_ROOT}/${MCU_VENDOR}/${MCU_SERIES}/src")
    message(WARNING "请参考 ${BSP_ROOT}/README.md 下载标准外设库")
    set(BSP_AVAILABLE FALSE)
endif()

# ============================================
# 添加子项目
# ============================================
# add_subdirectory(bootloader)  # 暂时跳过（GD32 相关）
add_subdirectory(app)
