cmake_minimum_required(VERSION 3.22)
project(blinky_f103 C ASM)

add_executable(blinky_f103
    src/main.c
    ${CMAKE_SOURCE_DIR}/bsp/common/syscalls.c
    ${CMAKE_SOURCE_DIR}/bsp/common/retarget.c
)

target_include_directories(blinky_f103 PRIVATE include)

target_link_libraries(blinky_f103 PRIVATE
    board_bluepill_f103c8
    freertos_kernel
)

target_link_options(blinky_f103 PRIVATE
    -Wl,-Map=${CMAKE_CURRENT_BINARY_DIR}/blinky_f103.map
)

# Generate .bin and .hex
add_custom_command(TARGET blinky_f103 POST_BUILD
    COMMAND ${CMAKE_OBJCOPY} -O binary $<TARGET_FILE:blinky_f103> ${CMAKE_CURRENT_BINARY_DIR}/blinky_f103.bin
    COMMAND ${CMAKE_OBJCOPY} -O ihex $<TARGET_FILE:blinky_f103> ${CMAKE_CURRENT_BINARY_DIR}/blinky_f103.hex
    COMMENT "Generating .bin and .hex for blinky_f103"
)
