# cmake/chips/stm32f103c8.cmake — STM32F103C8 芯片单一真相源
#
# 本文件是 STM32F103C8 全部"因芯片而异"构建事实的唯一来源，以 INTERFACE 库
# chip_stm32f103c8 承载，经依赖图 chip → board → app 向上传递。
#
# 【本文件只放"因芯片而异"的东西】
#   - 编译宏（STM32F103xB/STM32F1/USE_HAL_DRIVER/HSE_VALUE）
#   - CPU 选项（-mcpu=cortex-m3 -mthumb，编译+链接双侧）
#   - 启动文件 / 链接脚本（芯片专属，task-8 落位）
#   - FreeRTOS 移植标识 / 烧录型号（自定义属性）
#   - HAL 库依赖边（B1）
#
# 【全局 flag 不在此文件】
#   -specs=nano.specs / -Wl,--gc-sections / -fdata-sections / -ffunction-sections /
#   -O0/-O2 / C_STANDARD 11 / -Wall -Wextra 等归根 CMakeLists（task-11），
#   不因芯片而异。
#
# 【硬约束】F103 无 FPU：严禁添加 -mfloat-abi 或 -mfpu（任何形式）。
#   基线（stm32_template/CMakeLists.txt:26）仅 "-mcpu=cortex-m3 -mthumb"，
#   为保逐字节等价不得新增。float-abi/fpu 只属于 f4/f7/h7 分支。
#
# 契约来源：.omo/evidence/task-2-repo-shelf-restructure-contract.md（A/B/D/F 组）

add_library(chip_stm32f103c8 INTERFACE)

# ── (A) 编译宏 — 契约 A 组，逐字保留 ────────────────────────────────
#   来源：stm32_template/app/CMakeLists.txt:47-50 与 bsp/stm32/f1/CMakeLists.txt:27-30
target_compile_definitions(chip_stm32f103c8 INTERFACE
    STM32F103xB
    STM32F1
    USE_HAL_DRIVER
    HSE_VALUE=8000000
)

# ── (B) CPU 选项 — 契约 B 组，F103 无 FPU ───────────────────────────
#   来源：stm32_template/CMakeLists.txt:26（MCU_FLAGS）
#   编译期与链接期都必须带上同样的 CPU 选项，否则 multilib 选择变、产物不等价。
#   禁止新增 -mfloat-abi / -mfpu。
target_compile_options(chip_stm32f103c8 INTERFACE
    -mcpu=cortex-m3
    -mthumb
)
target_link_options(chip_stm32f103c8 INTERFACE
    -mcpu=cortex-m3
    -mthumb
)

# ── 启动文件 — 芯片专属汇编 ─────────────────────────────────────────
#   task-8 才把启动文件落位到 bsp/stm32/f1/startup/startup_stm32f103xb.s，
#   此处用变量占位。task-8 落位后需校验此路径存在。
set(CHIP_STM32F103C8_STARTUP
    ${CMAKE_CURRENT_LIST_DIR}/../../bsp/stm32/f1/startup/startup_stm32f103xb.s)
target_sources(chip_stm32f103c8 INTERFACE ${CHIP_STM32F103C8_STARTUP})

# ── 链接脚本 — 芯片专属内存映射 ─────────────────────────────────────
#   task-8 才把链接脚本落位到 bsp/stm32/f1/STM32F103XB_FLASH.ld，
#   此处用变量占位。task-8 落位后需校验此路径存在。
#   add_linker_script(TARGET VIS SCRIPT) 见 cmake/helpers.cmake:14（支持 INTERFACE 库）。
set(CHIP_STM32F103C8_LINKER_SCRIPT
    ${CMAKE_CURRENT_LIST_DIR}/../../bsp/stm32/f1/STM32F103XB_FLASH.ld)
add_linker_script(chip_stm32f103c8 INTERFACE ${CHIP_STM32F103C8_LINKER_SCRIPT})

# ── 自定义属性 — FreeRTOS 移植 / 烧录型号 ───────────────────────────
#   契约 D 组：FreeRTOS port = GCC/ARM_CM3 → 上游变量 FREERTOS_PORT=GCC_ARM_CM3
#   契约 F 组：J-Link device = STM32F103C8
#   供 board/app 层经 get_target_property 读取，驱动 FreeRTOS-Kernel 选型与烧录脚本。
set_property(TARGET chip_stm32f103c8 PROPERTY INTERFACE_FREERTOS_PORT GCC_ARM_CM3)
set_property(TARGET chip_stm32f103c8 PROPERTY INTERFACE_FLASH_DEVICE STM32F103C8)

# ── (B1 关键边) HAL 库依赖 — 契约 I 组 ──────────────────────────────
#   把 HAL 静态库接进依赖图：chip → bsp_stm32_f1。
#   bsp_stm32_f1（bsp/stm32/f1/CMakeLists.txt:20 STATIC）PUBLIC 导出 inc/
#   （含 stm32f1xx_hal.h，:22-24）。经 chip→board→app 传递，使 app 编译单元与
#   FreeRTOS 内核（FreeRTOSConfig.h include stm32f1xx_hal.h）都能见到 HAL 头。
#   真实 add_subdirectory(bsp/stm32/f1) 由 task-11 在根 CMake 完成。
target_link_libraries(chip_stm32f103c8 INTERFACE bsp_stm32_f1)
