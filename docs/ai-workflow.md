# AI 闭环开发工作流协议

> **读者：AI 代理（Atlas 等 LLM agent）**
> 本文档描述了 AI 如何与 STM32F103C8T6 嵌入式开发环境进行全自动闭环交互。

---

## 1. 协议概述

AI 通过以下三步形成闭环，零人工干预地完成嵌入式固件开发任务：

```
写代码 → 编译 → 烧录 → 读串口 → 解析 → 判断 → 没成继续改 → ...
```

- **写代码**：修改 `app/src/main.c`（或新增 `.c/.h` 文件）
- **编译**：`cmake --build build` 或 `bash tools/build_and_flash.sh --no-flash`
- **烧录**：`bash tools/build_and_flash.sh`（J-Link SWD）
- **读串口**：`python3 tools/serial_capture.py --timeout 10`
- **解析**：正则提取 `[LEVEL][MODULE] key=value ...` 格式的日志行
- **判断**：对比预期值和实际值，决定成功/失败
- **迭代**：失败后分析差异，修改代码，回到编译步骤

---

## 2. 环境信息

| 项目 | 值 |
|------|-----|
| **芯片** | STM32F103C8T6（Cortex-M3, 72MHz） |
| **时钟** | HSE 8MHz → PLL x9 → 72MHz |
| **Flash** | 64KB (0x08000000) |
| **RAM** | 20KB (0x20000000) |
| **编译器** | arm-none-eabi-gcc 15.3.1 |
| **构建** | CMake 4.3 + Ninja 1.13 |
| **烧录器** | J-Link (SWD, 4000kHz) |
| **串口** | USART1, PA9(TX) PA10(RX), 115200 8N1 无流控 |
| **LED** | PA0, PA1（高电平点亮） |
| **看门狗** | IWDG, ~6.5秒超时（LSI 40kHz / 64 × 4095） |
| **SysTick** | 1ms 中断，驱动 `g_uptime_ms` 全局变量 |

### 引脚分配

| 引脚 | 功能 | 方向 | 备注 |
|------|------|------|------|
| PA0 | LED0 | 输出 | 高电平点亮 |
| PA1 | LED1 | 输出 | 高电平点亮 |
| PA9 | USART1_TX | 输出 | AF_PP, 接 USB-UART 的 RX |
| PA10 | USART1_RX | 输入 | 浮空, 接 USB-UART 的 TX |
| PA13 | SWDIO | — | J-Link 调试 |
| PA14 | SWCLK | — | J-Link 调试 |

### 关键注意事项
- **BOOT0 必须接 GND**，否则芯片不跑 Flash 里的程序
- **F103 无 FPU**，不要使用 `float`/`double` 运算
- **启动文件已修复**（`app/startup/startup_stm32f103xb.s`）：cpsid i + bx lr + 跳过 __libc_init_array

---

## 3. 命令参考

### 3.1 编译并烧录

```bash
cd /Users/seven.xu/code/stm32_template
bash tools/build_and_flash.sh
```

选项：
- `--no-flash`：只编译，不烧录

### 3.2 只编译

```bash
cd /Users/seven.xu/code/stm32_template
cmake --build build
```

编译产物：
- `build/app/app.elf` — 带调试信息
- `build/app/app.bin` — 纯二进制（烧录用）
- `build/app/app.hex` — Intel HEX

### 3.3 捕获串口输出

```bash
cd /Users/seven.xu/code/stm32_template
python3 tools/serial_capture.py --timeout 10
```

选项：
- `--device /dev/tty.xxx` — 手动指定串口设备
- `--baud 115200` — 指定波特率
- `--timeout 15` — 捕获超时秒数
- `--list` — 列出所有可用串口

### 3.4 首次配置 CMake（如需）

```bash
cd /Users/seven.xu/code/stm32_template
cmake -B build -G Ninja -DCMAKE_TOOLCHAIN_FILE=cmake/arm-none-eabi-gcc.cmake
```

---

## 4. 串口输出格式

所有固件日志遵循固定格式：

```
[LEVEL][MODULE] key=value key2=value2 ...
```

### 日志级别

| LEVEL | 含义 | 行为 |
|-------|------|------|
| `OK` | 预期行为确认 | 正常输出 |
| `DBG` | 调试信息 | 正常输出 |
| `WARN` | 警告（可恢复） | 正常输出 |
| `ERR` | 错误（操作失败） | 正常输出 |
| `FATAL` | 致命错误 | 输出后 **停止执行**（while(1)死锁，看门狗复位） |

### 模块名（MODULE）

当前固件使用的模块名：
- `boot` — 启动/初始化相关
- `led` — LED 控制
- `serial` — 串口相关（Python 脚本侧）

### 解析正则表达式

```
^\[(OK|DBG|WARN|ERR|FATAL)\]\[(\w+)\]\s+(.+)$
```

捕获组：
- `$1` — 日志级别
- `$2` — 模块名
- `$3` — 消息体（`key=value ...` 键值对）

### 示例输出

```
[BOOT] STM32F103C8T6 AI Debug System
[OK][boot] init_complete uptime_ms=45 sysclk=72000000
[OK][led] toggle uptime_ms=1500 state=A
[OK][led] toggle uptime_ms=3000 state=B
[OK][led] toggle uptime_ms=4500 state=A
```

---

## 5. 闭环迭代流程

AI 执行固件开发任务的完整流程：

### Step 1：理解任务
- 读取 `app/src/main.c` 理解当前固件逻辑
- 确定需要修改的代码位置

### Step 2：修改代码
- 编辑 `app/src/main.c`（或创建/修改其他 `.c/.h` 文件）
- 如新增 `.h` 文件，放到 `app/include/`
- 如新增 `.c` 文件，放到 `app/src/` 并更新 `app/CMakeLists.txt` 中的 `SOURCES`

### Step 3：编译 + 烧录
```bash
bash tools/build_and_flash.sh
```
- 编译失败 → 分析错误信息，回到 Step 2
- 烧录失败 → 检查硬件连接（J-Link、供电、BOOT0=GND）

### Step 4：捕获串口输出
```bash
python3 tools/serial_capture.py --timeout 10
```
- 脚本的 stderr 输出（`[OK][serial]` 等）可忽略
- 关注 stdout 中的 STM32 日志

### Step 5：解析串口输出
- 用正则 `^\[(OK|DBG|WARN|ERR|FATAL)\]\[(\w+)\]\s+(.+)$` 提取日志行
- 从消息体中解析 `key=value` 键值对
- 提取你关心的数据（如 `uptime_ms`, `state`, `value` 等）

### Step 6：判断成败
- 将解析结果与预期值对比
- **成功**：数据在容差范围内 → 记录证据，报告完成
- **失败**：数据不符合预期 → 分析差异原因 → 回到 Step 2

### Step 7：迭代记录
- 每轮迭代写入 `.omo/evidence/smoke-test-iter-N.log`
- 记录：本轮代码变更 + 串口输出全文 + 解析结果 + 判断结论

---

## 6. 安全机制

| 机制 | 说明 |
|------|------|
| **最大迭代** | 每个任务最多 20 轮。超过仍未成功 → 报告失败并停止 |
| **单轮超时** | 烧录+串口捕获每轮不超过 60 秒 |
| **IWDG 看门狗** | 固件侧 ~6.5 秒超时。主循环不喂狗 → MCU 自动复位 → 串口看到第二次 `[BOOT]` 消息 |
| **FATAL 日志** | 固件输出 FATAL 后死锁 → 看门狗复位 → AI 应识别复位模式 |
| **编译失败** | 编译错误不烧录，AI 必须修复代码再重试 |

---

## 7. 常见问题

### 串口无输出
1. TX/RX 是否接反？（STM32 TX → USB-UART RX, STM32 RX ← USB-UART TX）
2. GND 是否连接？
3. 波特率是否是 115200？
4. 用 `python3 tools/serial_capture.py --list` 确认设备存在

### 烧录失败
1. J-Link USB 是否连接？`which JLinkExe` 输出路径？
2. 板子是否通电？
3. BOOT0 是否接 GND？
4. SWD 接线：VTref→3.3V, GND→GND, SWDIO→PA13, SWCLK→PA14

### 串口乱码
1. 波特率是否匹配？（固件 115200，脚本默认 115200）
2. 时钟配置是否正确？（HSE 8MHz → PLL x9 → 72MHz）
3. USART1 在 APB2 总线上（72MHz），波特率计算：72000000 / (16 × 39.0625) ≈ 115200

### 看门狗频繁复位
- 主循环耗时过长 → 增加喂狗频率或调大超时（修改 `IWDG_Init()` 中的 `Prescaler` 或 `Reload` 值）

### 编译报错 "undefined reference to ..."
- 可能是新增了 `.c` 文件但没加到 `app/CMakeLists.txt` 的 `SOURCES` 列表中

---

## 8. 代码结构

```
stm32_template/
├── app/
│   ├── src/
│   │   └── main.c              # 主固件代码（唯一需要修改的文件）
│   ├── include/
│   │   └── debug.h             # 日志宏（不要改）
│   ├── startup/
│   │   └── startup_stm32f103xb.s  # 启动文件（已修复，不要改）
│   ├── link/
│   │   └── stm32f103_app.ld    # 链接脚本（不要改）
│   └── CMakeLists.txt          # 构建配置（加文件时改 SOURCES）
├── tools/
│   ├── build_and_flash.sh      # 一键编译烧录
│   └── serial_capture.py       # 串口捕获
├── docs/
│   └── ai-workflow.md           # 本文档
├── flash_app.jlink             # J-Link 烧录脚本
├── CMakeLists.txt              # 主构建配置（不要改芯片配置）
└── build/                      # 编译产物（不要编辑）
    └── app/
        ├── app.elf
        ├── app.bin
        └── app.hex
```

---

## 9. 任务示例

### 任务：改 LED 闪烁周期从 1.5 秒到 0.5 秒

1. **读代码** → 找到 `delay(1500000)` 两处
2. **改代码** → 改为 `delay(500000)`
3. **编译烧录** → `bash tools/build_and_flash.sh`
4. **捕获串口** → `python3 tools/serial_capture.py --timeout 10`
5. **解析** → 提取两个 `[OK][led] toggle` 行的 `uptime_ms` 值，计算差值
6. **判断** → 差值 ≈ 500ms（±200ms 容差）？是 → 成功；否 → 调整 delay 值重试
