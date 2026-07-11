# App 项目配置

set(TARGET_NAME app)

# 源文件
set(SOURCES
    src/main.c
    startup/startup_stm32f103xb.s
)

# 添加通用代码
list(APPEND SOURCES
    ${BSP_ROOT}/common/syscalls.c
    ${BSP_ROOT}/common/retarget.c
)

# FreeRTOS 源文件
set(FREERTOS_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/../Middlewares/FreeRTOS)
set(FREERTOS_SOURCES
    ${FREERTOS_ROOT}/tasks.c
    ${FREERTOS_ROOT}/queue.c
    ${FREERTOS_ROOT}/list.c
    ${FREERTOS_ROOT}/portable/GCC/ARM_CM3/port.c
    ${FREERTOS_ROOT}/portable/MemMang/heap_4.c
)
list(APPEND SOURCES ${FREERTOS_SOURCES})

# 头文件目录
include_directories(
    include
    ../common/include
    ${FREERTOS_ROOT}/include
    ${FREERTOS_ROOT}/portable/GCC/ARM_CM3
)

# 链接脚本
set(LINKER_SCRIPT ${CMAKE_CURRENT_SOURCE_DIR}/link/stm32f103_app.ld)

# 生成可执行文件
add_executable(${TARGET_NAME}.elf ${SOURCES})

# FreeRTOS 源文件编译选项（内部有未用参数等）
set_source_files_properties(${FREERTOS_SOURCES} PROPERTIES COMPILE_OPTIONS "-Wno-unused-parameter;-Wno-sign-compare")

# 编译定义（与 BSP CMake 对齐）
target_compile_definitions(${TARGET_NAME}.elf PRIVATE
    STM32F103xB
    STM32F1
    USE_HAL_DRIVER
    HSE_VALUE=8000000
)

# 如果 BSP 可用，链接 BSP 库
if(BSP_AVAILABLE)
    target_link_libraries(${TARGET_NAME}.elf PRIVATE ${BSP_TARGET})
    message(STATUS "App: 已链接 BSP 库")
endif()

# 链接选项
target_link_options(${TARGET_NAME}.elf PRIVATE
    -T${LINKER_SCRIPT}
    -Wl,-Map=${CMAKE_CURRENT_BINARY_DIR}/${TARGET_NAME}.map
)

# 生成 hex 和 bin 文件
add_custom_command(TARGET ${TARGET_NAME}.elf POST_BUILD
    COMMAND ${CMAKE_OBJCOPY} -O ihex $<TARGET_FILE:${TARGET_NAME}.elf> ${TARGET_NAME}.hex
    COMMAND ${CMAKE_OBJCOPY} -O binary $<TARGET_FILE:${TARGET_NAME}.elf> ${TARGET_NAME}.bin
    COMMAND ${CMAKE_SIZE} $<TARGET_FILE:${TARGET_NAME}.elf>
    COMMENT "Building ${TARGET_NAME}.hex and ${TARGET_NAME}.bin"
)
