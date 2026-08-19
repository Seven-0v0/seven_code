cmake_minimum_required(VERSION 3.20)
project(seven_code C ASM)

# ============================================
# Helpers
# ============================================
include(cmake/helpers.cmake)

# ============================================
# Global Flags (MUST be set BEFORE any add_subdirectory)
# ============================================
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -fdata-sections -ffunction-sections -Wall -Wextra")
set(CMAKE_C_FLAGS_DEBUG "-O0 -g3")
set(CMAKE_C_FLAGS_RELEASE "-O2")
set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -specs=nano.specs -lc -lm -Wl,--gc-sections")
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# ============================================
# Target selection
# ============================================
set(TARGET_BOARD "f103" CACHE STRING "Firmware board target")
set_property(CACHE TARGET_BOARD PROPERTY STRINGS f103 rm_dev_board_c)

if(TARGET_BOARD STREQUAL "f103")
    # Preserve the established BluePill build as the default target.
    add_subdirectory(bsp/stm32/f1)
    include(cmake/chips/stm32f103c8.cmake)
    add_subdirectory(boards/bluepill_f103c8)
    set(SELECTED_CHIP chip_stm32f103c8)
    set(SELECTED_APP blinky_f103)
elseif(TARGET_BOARD STREQUAL "rm_dev_board_c")
    # The F4 BSP is configured from facts published by the selected chip target.
    include(boards/rm_dev_board_c/board.cmake)
    include(cmake/chips/stm32f407ighx.cmake)
    add_subdirectory(bsp/stm32/f4)
    add_subdirectory(boards/rm_dev_board_c)
    # Portable units this board's app links. Added only in this branch so the
    # f103 target's source set stays exactly as it was.
    add_subdirectory(drivers/imu/bmi088)
    add_subdirectory(drivers/util/fmt)
    set(SELECTED_CHIP chip_stm32f407ighx)
    set(SELECTED_APP rm_c_blinky)
else()
    message(FATAL_ERROR "Unsupported TARGET_BOARD='${TARGET_BOARD}'. Use f103 or rm_dev_board_c.")
endif()

# ============================================
# Middleware Layer - FreeRTOS
# ============================================
get_target_property(FREERTOS_PORT ${SELECTED_CHIP} INTERFACE_FREERTOS_PORT)
set(FREERTOS_HEAP 4)
add_subdirectory(middleware/FreeRTOS-Kernel)

# ============================================
# Application Layer
# ============================================
add_subdirectory(apps/${SELECTED_APP})
