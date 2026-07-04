# App 项目配置

set(TARGET_NAME app)

# 源文件
set(SOURCES
    src/verify_stm32_main.c
    startup/startup_stm32f407xx.s
)

# 添加通用代码
list(APPEND SOURCES
    ${BSP_ROOT}/common/syscalls.c
    ${BSP_ROOT}/common/retarget.c
)

# 头文件目录
include_directories(
    include
    ../common/include
)

# 链接脚本
set(LINKER_SCRIPT ${CMAKE_CURRENT_SOURCE_DIR}/link/gd32f4xx_app.ld)

# 生成可执行文件
add_executable(${TARGET_NAME}.elf ${SOURCES})

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
