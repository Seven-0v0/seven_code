# middleware/ — 共享中间件

## 这一层放什么

**跨项目共享的中间件**：从各项目里抽出来的公共组件，最典型的是 **FreeRTOS-Kernel**，以及未来可能的文件系统、CLI、日志框架、网络协议栈等。

## 什么该放这里

- FreeRTOS 内核源（`tasks.c` / `queue.c` / `list.c` / `port.c` / `heap_*.c` 等）。
  - ⚠️ 注意 vendored 内核 `.c` 位于其**根目录**，**不在** `Source/` 子目录下（见 task-2 契约与 stm32-freertos-port learnings）。用 `${FREERTOS_ROOT}/Source/tasks.c` 会失败。
- 内核编译单元的唯一真相：port 由芯片表折算的 `FREERTOS_PORT`（如 `GCC_ARM_CM3`）、heap 由 `FREERTOS_HEAP`（如 `4`）选择，**防止内核被编两次或零次**。

## 什么不该放这里

- **不要**放 `FreeRTOSConfig.h`——该配置头由 `boards/` 提供并注入（板级差异）。
- 不要放芯片/板级/应用专属代码。
- 不要放外设驱动（属于 `drivers/`）。

## 命名约定

- 一个中间件一个子目录：`middleware/<name>/`，如 `middleware/FreeRTOS-Kernel/`。

## 与相邻层的关系（依赖方向）

中间件是共享底座。
- FreeRTOS 内核由 `boards/` 注入 `FreeRTOSConfig.h`（配置来自板级），由 `apps/` 链接进最终固件。
- 内核只编译一次，port/heap 的选择来自芯片表变量。
依赖方向：`board` 注入配置 → `app` 链接 middleware；middleware 本身不反向依赖 board/app。
