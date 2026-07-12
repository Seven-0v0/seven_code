add_library(board_bluepill_f103c8 INTERFACE)
target_link_libraries(board_bluepill_f103c8 INTERFACE chip_stm32f103c8)

# freertos_config 反转依赖：内核编译 port.c/tasks.c 时需要 FreeRTOSConfig.h + stm32f1xx_hal.h
add_library(freertos_config INTERFACE)
target_include_directories(freertos_config INTERFACE ${CMAKE_CURRENT_LIST_DIR})
target_link_libraries(freertos_config INTERFACE chip_stm32f103c8)
