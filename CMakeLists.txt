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
# BSP Layer (MUST come before chip include for B1 link edge)
# ============================================
add_subdirectory(bsp/stm32/f1)

# ============================================
# Chip Configuration
# ============================================
include(cmake/chips/stm32f103c8.cmake)

# ============================================
# Board Layer (MUST come before FreeRTOS for freertos_config target)
# ============================================
add_subdirectory(boards/bluepill_f103c8)

# ============================================
# Middleware Layer - FreeRTOS
# ============================================
set(FREERTOS_PORT GCC_ARM_CM3)
set(FREERTOS_HEAP 4)
add_subdirectory(middleware/FreeRTOS-Kernel)

# ============================================
# Application Layer
# ============================================
add_subdirectory(apps/blinky_f103)
