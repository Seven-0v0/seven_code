# AI 闭环开发系统 — 踩坑记录

> 记录了将 STM32F103C8T6 闪灯 demo 改造为 AI 可全自动闭环开发系统过程中遇到的所有坑。

---

## 改动了什么

### 新增文件（5 个）

| 文件 | 用途 |
|------|------|
| `stm32_template/app/src/main.c` | 新固件：LED 交替闪烁 + UART1 串口输出 + SysTick 计时 + IWDG 看门狗 |
| `stm32_template/app/include/debug.h` | 结构化日志宏 `[LEVEL][MODULE] key=value`，AI 可正则解析 |
| `stm32_template/tools/serial_capture.py` | Python 脚本，捕获 STM32 串口输出（pyserial, macOS） |
| `stm32_template/tools/build_and_flash.sh` | 一键编译烧录（cmake build → J-Link flash） |
| `stm32_template/docs/ai-workflow.md` | AI 开发工作流协议文档（7 个章节，275 行） |

### 修改文件（2 个）

| 文件 | 改动 | 原因 |
|------|------|------|
| `stm32_template/app/CMakeLists.txt` | `verify_f103_main.c` → `main.c` | 切换到新的统一固件文件 |
| `stm32_template/app/src/main.c` | 从 placeholder 空循环 → 全功能固件 | 原 main.c 只有 `delay(1000000)` 空循环 |

---

## 踩坑记录

### 坑 1：`retarget_init()` 隐式声明 → 编译失败

**现象：** 编译报错 `implicit declaration of function 'retarget_init'`

**原因：** `bsp/common/retarget.c` 中定义了 `retarget_init()`，但没有头文件。`bsp/common/` 目录下没有任何 `.h` 文件。

**解决：** 删除 `retarget_init()` 调用。该函数是空实现，且 `_write()` 直接调用弱符号 `uart_send_byte()`，只需在 main.c 中提供强符号实现即可。

---

### 坑 2：SysTick 不触发，`uptime_ms` 始终为 0（核心坑）

**现象：** 串口输出 `hal=0 uptime=0`，`HAL_GetTick()` 也返回 0。

**两层根因：**

#### 第一层：`cpsid i` 禁用了全局中断

启动文件 `startup_stm32f103xb.s` 的 `Reset_Handler` 开头有 `cpsid i`（见 `docs/startup-fix.md`）。修复 bare metal 启动问题时加上去的，但整个程序运行期间没恢复——所有中断（包括 SysTick）都不触发。

**修复：** `main()` 中 `SystemClock_Config()` 后加 `__enable_irq()`。

#### 第二层：`SysTick_Handler` 是弱符号 → `Default_Handler(bx lr)` 什么也不做

```asm
.weak SysTick_Handler
.thumb_set SysTick_Handler, Default_Handler
```

`Default_Handler` 已修复为 `bx lr`（直接返回），所以 SysTick 中断触发后什么也不做，`HAL_IncTick()` 永远不会被调用。

**修复：** 在 main.c 中提供强符号实现：

```c
void SysTick_Handler(void)
{
    HAL_IncTick();
    g_uptime_ms++;
}
```

---

### 坑 3：delay() 与 wall-clock 不是线性关系

**现象：** `delay(1500000)` 预期 ~1500ms，实际 ~297ms。

**原因：** Flash 等待周期 `FLASH_LATENCY_2` 导致取指慢 3 倍，加上 for 循环开销，实际比例约 5000 cycles/ms。

| delay 值 | 实际间隔 | 比例 |
|----------|---------|------|
| 500,000 | ~102ms | ~4,900 |
| 1,500,000 | ~297ms | ~5,050 |
| 1,800,000 | ~355ms | ~5,070 |
| 2,500,000 | ~491ms | ~5,090 |

**策略：** 用串口反馈的 SysTick 时间反推正确的 delay 值，不靠猜。

---

### 坑 4：启动消息丢失

**现象：** `[BOOT] STM32F103C8T6...` 从未出现在串口输出中。

**原因：** 烧录后 MCU 立即启动，printf 启动消息在几百毫秒内发出。但 Python 串口脚本连接需要 ~2 秒，此时消息已发出且无缓冲。

**解决：** 接受限制。主循环中的 `[OK][led] toggle` 消息足以验证系统正常运行。

---

### 坑 5：J-Link VCOM vs STM32 UART 混淆

**现象：** `serial_capture.py --list` 显示 `/dev/cu.usbmodem0006027122251` 标注为 "J-Link"。

**原因：** 该设备是 J-Link 的内置 VCOM（VID:PID=1366:0105 = SEGGER）。用户需将 J-Link 的 VCOM 引脚接 STM32 的 PA9/PA10 才能读到数据。

---

## 关键经验

1. **bare metal 中断管理**：cpsid i / __enable_irq / Default_Handler 是隐藏陷阱
2. **SysTick 是唯一可靠的时间源**：软件 delay 不可靠
3. **闭环 = 反馈驱动**：用串口数据计算修正比例，不靠猜
4. **启动消息不可靠**：关键验证放主循环
5. **物理接线确认 > 软件检测**
