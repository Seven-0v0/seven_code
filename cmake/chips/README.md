# cmake/chips/ — 每颗芯片一个配置表

## 这一层放什么

**每颗芯片一个 `<model>.cmake` 文件**，作为该芯片所有编译期事实的**单一真相源（single source of truth）**。例如 `stm32f103c8.cmake`。

一个芯片配置表集中定义：

- **CPU 选项**：如 `-mcpu=cortex-m3 -mthumb`。
  - ⚠️ F103 无 FPU，**禁止**新增 `-mfloat-abi` 之类浮点 ABI（见 task-2 契约）。
- **编译宏**：芯片系列宏、HAL 使能宏等（如 `STM32F103xB`、`USE_HAL_DRIVER`）。
- **启动文件**：该芯片的 `startup_*.s` 路径（通常指向 `bsp/` 内厂商源）。
- **链接脚本**：该芯片的 `.ld` 路径。
- **FreeRTOS 移植参数**：折算成上游变量，如 `FREERTOS_PORT=GCC_ARM_CM3`、`FREERTOS_HEAP=4`。
- **烧录型号**：J-Link device 名（如 `STM32F103C8`），供烧录脚本引用。

## 什么该放这里

- 与"这是哪颗芯片"绑定、但与"哪块板子"无关的一切编译期配置。

## 什么不该放这里

- **禁止**放全局编译 flag（`-Wall -Wextra -fdata-sections -ffunction-sections`、C11、`-O0/-O2`、`-specs=nano.specs` 等）——这些是全局项，归根 `CMakeLists.txt`（见 task-2 契约 C）。
- 不要放引脚映射、时钟配置、`FreeRTOSConfig.h`（属于 `boards/`）。
- 不要放编译器可执行文件路径（属于 `cmake/toolchain/`）。

## 命名约定

- 文件名 = 芯片型号小写：`<model>.cmake`，如 `stm32f103c8.cmake`、`gd32f303cc.cmake`。

## 与相邻层的关系（依赖方向）

芯片表位于工具链之上、板级之下。它引用 `bsp/` 里对应芯片的厂商源、启动文件、链接脚本，但只负责"选中并参数化"，不重复实现。
`boards/<board>/` 会 include 对应的芯片表来确定 MCU，然后叠加板级差异（引脚/时钟/RTOS 配置头）。
