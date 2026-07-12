# helpers.cmake — 通用 CMake 辅助函数（不含任何芯片专属逻辑）
#
# add_linker_script(TARGET VIS SCRIPT)
#   给目标挂载链接脚本，并把脚本登记为链接依赖，链接脚本改动时触发重新链接。
#
#   参数：
#     TARGET  目标名（可执行目标或 INTERFACE 库）
#     VIS     可见性：普通可执行目标用 PRIVATE，INTERFACE 库用 INTERFACE
#     SCRIPT  链接脚本（.ld）路径
#
#   用法：
#     add_linker_script(my_exe PRIVATE ${CMAKE_SOURCE_DIR}/link.ld)
#     add_linker_script(chip_stm32f103c8 INTERFACE ${CHIP_LD})
function(add_linker_script TARGET VIS SCRIPT)
    # 用 -T 挂载链接脚本
    target_link_options(${TARGET} ${VIS} "-T" "${SCRIPT}")
    # 登记为链接依赖，脚本改动即触发重新链接
    set_property(TARGET ${TARGET} APPEND PROPERTY INTERFACE_LINK_DEPENDS "${SCRIPT}")
endfunction()
