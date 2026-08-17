# STM32F407IGHx chip facts: Cortex-M4F, 1 MiB flash, 128 KiB SRAM, 64 KiB CCM.
# Board-specific pins and clock-tree selection remain in boards/rm_dev_board_c.

add_library(chip_stm32f407ighx INTERFACE)
set(STM32_F4_CHIP_TARGET chip_stm32f407ighx)

set(CHIP_STM32F407IGHX_DEVICE_DEFINE STM32F407xx)
set(CHIP_STM32F407IGHX_CPU_OPTIONS
    -mcpu=cortex-m4
    -mthumb
    -mfpu=fpv4-sp-d16
    -mfloat-abi=hard
)

target_compile_definitions(chip_stm32f407ighx INTERFACE
    ${CHIP_STM32F407IGHX_DEVICE_DEFINE}
    STM32F4xx
    USE_HAL_DRIVER
)
target_compile_options(chip_stm32f407ighx INTERFACE ${CHIP_STM32F407IGHX_CPU_OPTIONS})
target_link_options(chip_stm32f407ighx INTERFACE ${CHIP_STM32F407IGHX_CPU_OPTIONS})
set_property(TARGET chip_stm32f407ighx PROPERTY STARTUP_SOURCE
    ${CMAKE_CURRENT_LIST_DIR}/../../bsp/stm32/f4/startup/startup_stm32f407xx.s)
add_linker_script(chip_stm32f407ighx INTERFACE
    ${CMAKE_CURRENT_LIST_DIR}/../../bsp/stm32/f4/STM32F407IG_FLASH.ld
)

set_property(TARGET chip_stm32f407ighx PROPERTY STM32F4_DEVICE_DEFINE
    ${CHIP_STM32F407IGHX_DEVICE_DEFINE})
set_property(TARGET chip_stm32f407ighx PROPERTY STM32F4_CPU_OPTIONS
    "${CHIP_STM32F407IGHX_CPU_OPTIONS}")
set_property(TARGET chip_stm32f407ighx PROPERTY INTERFACE_FREERTOS_PORT GCC_ARM_CM4F)
set_property(TARGET chip_stm32f407ighx PROPERTY INTERFACE_FLASH_DEVICE STM32F407IG)
