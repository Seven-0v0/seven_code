cmake_minimum_required(VERSION 3.20)
project(bsp_stm32_f4 C ASM)

# STM32F4xx BSP 库。芯片目标先发布设备和时钟事实，本层只消费它们。
if(NOT EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/src)
    message(FATAL_ERROR "STM32F4xx HAL/CMSIS source directory is required")
endif()

if(NOT DEFINED STM32_F4_CHIP_TARGET OR NOT TARGET ${STM32_F4_CHIP_TARGET})
    message(FATAL_ERROR "Configure an STM32F4 chip target before its BSP")
endif()

get_target_property(F4_DEVICE_DEFINE ${STM32_F4_CHIP_TARGET} STM32F4_DEVICE_DEFINE)
get_target_property(F4_CPU_OPTIONS ${STM32_F4_CHIP_TARGET} STM32F4_CPU_OPTIONS)

if(NOT F4_DEVICE_DEFINE OR NOT F4_CPU_OPTIONS)
    message(FATAL_ERROR "STM32F4 chip target must define device and CPU facts")
endif()
if(NOT DEFINED BOARD_HSE_VALUE)
    message(FATAL_ERROR "The selected STM32F4 board must define BOARD_HSE_VALUE")
endif()

file(GLOB BSP_SOURCES CONFIGURE_DEPENDS
    ${CMAKE_CURRENT_SOURCE_DIR}/src/*.c
)
list(FILTER BSP_SOURCES EXCLUDE REGEX ".*_template\\.c$")

add_library(bsp_stm32_f4 STATIC ${BSP_SOURCES})

target_include_directories(bsp_stm32_f4 PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/inc
)

target_compile_definitions(bsp_stm32_f4 PUBLIC
    ${F4_DEVICE_DEFINE}
    STM32F4xx
    USE_HAL_DRIVER
    HSE_VALUE=${BOARD_HSE_VALUE}
)

target_compile_options(bsp_stm32_f4 PRIVATE
    ${F4_CPU_OPTIONS}
    -Wall
    -Wextra
    -Wno-unused-parameter
)

target_link_libraries(${STM32_F4_CHIP_TARGET} INTERFACE bsp_stm32_f4)
message(STATUS "STM32F4xx BSP configured for ${F4_DEVICE_DEFINE}, HSE=${BOARD_HSE_VALUE}Hz")
