add_library(board_rm_dev_board_c INTERFACE)
target_sources(board_rm_dev_board_c INTERFACE
    ${CMAKE_CURRENT_LIST_DIR}/src/board.c
    ${CMAKE_CURRENT_LIST_DIR}/src/board_imu_spi.c
)
target_include_directories(board_rm_dev_board_c INTERFACE ${CMAKE_CURRENT_LIST_DIR})
target_compile_definitions(board_rm_dev_board_c INTERFACE HSE_VALUE=${BOARD_HSE_VALUE})
target_link_libraries(board_rm_dev_board_c INTERFACE chip_stm32f407ighx)

# The RTOS compiles through this target, so it receives the selected chip's FPU ABI.
add_library(freertos_config INTERFACE)
target_include_directories(freertos_config INTERFACE ${CMAKE_CURRENT_LIST_DIR})
target_link_libraries(freertos_config INTERFACE chip_stm32f407ighx)
