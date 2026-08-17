add_executable(rm_c_blinky src/main.c)

get_target_property(CHIP_STARTUP_SOURCE chip_stm32f407ighx STARTUP_SOURCE)
target_sources(rm_c_blinky PRIVATE ${CHIP_STARTUP_SOURCE})

target_link_libraries(rm_c_blinky PRIVATE
    board_rm_dev_board_c
    freertos_kernel
)

target_link_options(rm_c_blinky PRIVATE
    -Wl,-Map=${CMAKE_CURRENT_BINARY_DIR}/rm_c_blinky.map
)

add_custom_command(TARGET rm_c_blinky POST_BUILD
    COMMAND ${CMAKE_OBJCOPY} -O binary $<TARGET_FILE:rm_c_blinky> ${CMAKE_CURRENT_BINARY_DIR}/rm_c_blinky.bin
    COMMAND ${CMAKE_OBJCOPY} -O ihex $<TARGET_FILE:rm_c_blinky> ${CMAKE_CURRENT_BINARY_DIR}/rm_c_blinky.hex
    COMMENT "Generating .bin and .hex for rm_c_blinky"
)
