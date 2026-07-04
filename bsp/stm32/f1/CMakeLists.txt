cmake_minimum_required(VERSION 3.20)
project(bsp_stm32_f1 C ASM)

# STM32F1xx BSP 库配置

if(NOT EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/src)
    message(WARNING "STM32F1xx HAL 库未找到")
    return()
endif()

set(BSP_TARGET bsp_stm32_f1)

file(GLOB BSP_SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/src/*.c)
list(FILTER BSP_SOURCES EXCLUDE REGEX ".*_template\\.c$")

if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/src/system_stm32f1xx.c)
    list(APPEND BSP_SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/src/system_stm32f1xx.c)
endif()

add_library(${BSP_TARGET} STATIC ${BSP_SOURCES})

target_include_directories(${BSP_TARGET} PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/inc
)

target_compile_definitions(${BSP_TARGET} PUBLIC
    STM32F103xB
    STM32F1
    USE_HAL_DRIVER
    HSE_VALUE=8000000
)

# Cortex-M3, no FPU
target_compile_options(${BSP_TARGET} PRIVATE
    -mcpu=cortex-m3
    -mthumb
    -Wall
    -Wextra
    -Wno-unused-parameter
)

message(STATUS "STM32F1xx BSP 库已配置")
