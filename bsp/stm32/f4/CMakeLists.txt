cmake_minimum_required(VERSION 3.20)
project(bsp_stm32_f4 C ASM)

# STM32F4xx BSP 库配置

# 检查标准外设库是否存在
if(NOT EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/src)
    message(WARNING "STM32F4xx 标准外设库未找到，请从官网下载：https://www.st.com/")
    return()
endif()

# 库名称
set(BSP_TARGET bsp_stm32_f4)

# 收集所有源文件
file(GLOB BSP_SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/src/*.c
)

# 如果有系统文件
if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/src/system_stm32f4xx.c)
    list(APPEND BSP_SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/src/system_stm32f4xx.c)
endif()

# 排除模板文件（不应该编译）
list(FILTER BSP_SOURCES EXCLUDE REGEX ".*_template\\.c$")

# 创建静态库
add_library(${BSP_TARGET} STATIC ${BSP_SOURCES})

# 头文件目录
target_include_directories(${BSP_TARGET} PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/inc
)

# 编译定义
target_compile_definitions(${BSP_TARGET} PUBLIC
    STM32F407xx
    STM32F4xx
    USE_STDPERIPH_DRIVER
    HSE_VALUE=8000000
)

# 编译选项
target_compile_options(${BSP_TARGET} PRIVATE
    -mcpu=cortex-m4
    -mthumb
    -mfpu=fpv4-sp-d16
    -mfloat-abi=hard
    -Wall
    -Wextra
    -Wno-unused-parameter
)

message(STATUS "STM32F4xx BSP 库已配置")
