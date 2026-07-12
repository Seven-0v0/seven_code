# boards/ — 板级支持

## 这一层放什么

**每块板子一个子目录**。一块板子 = 一颗芯片 + 引脚映射 + 时钟配置 + `FreeRTOSConfig.h`。

典型子目录结构：

```
boards/<board_name>/
├── board.cmake          # include 对应 cmake/chips/<model>.cmake，声明本板选用的芯片
├── pins.h / board.h     # 引脚映射（LED、串口、按键等 GPIO 定义）
├── clock config          # 系统时钟/HSE/PLL 配置
└── FreeRTOSConfig.h     # 本板的 FreeRTOS 配置（tick、堆、优先级等）
```

## 什么该放这里

- 引脚映射（哪个 GPIO 接了什么外设）。
- 时钟树配置（晶振频率、PLL 倍频）。
- `FreeRTOSConfig.h`：由板级提供，注入到 middleware 的 FreeRTOS 内核。
- 选定芯片：通过 include `cmake/chips/<model>.cmake`。

## 什么不该放这里

- 不要放芯片级编译宏/CPU 选项/启动文件/链接脚本（属于 `cmake/chips/`）。
- 不要放可移植的设备驱动实现（属于 `drivers/`）。
- 不要放产品/应用逻辑（属于 `apps/`）。
- 不要在这里编译 FreeRTOS 内核源（内核在 `middleware/`，板级只提供 config 头）。

## 命名约定

- 子目录名 = 板子名，简洁可辨识，如 `bluepill_f103c8/`、`nucleo_f103rb/`。

## 与相邻层的关系（依赖方向）

板级位于芯片之上、应用之下。
- 向下：选一颗芯片（include `cmake/chips/<model>.cmake`），依赖 `bsp/` 的厂商 HAL。
- 向侧：向 `middleware/` 的 FreeRTOS 注入 `FreeRTOSConfig.h`。
- 向上：被 `apps/` 选用。
依赖方向：`app → board → chip → bsp`。
