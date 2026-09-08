# 嵌入式固件开发货架

**货架化嵌入式仓库** — 多芯片、多板子、多项目的清晰分层架构。

## 🎯 设计理念

- **货架化**：芯片、板子、中间件、驱动、应用各就各位，按稳定性分层
- **可复用**：FreeRTOS、BSP、驱动等共享组件只维护一次
- **可扩展**：新项目选一块板子即可开始，新板子选一颗芯片即可创建
- **单一真相源**：每颗芯片的编译配置、启动文件、链接脚本在一处定义

## 📁 目录结构（5 层货架）

```
.
├── bsp/                          # 芯片 BSP 层（厂商外设库、启动文件）
│   ├── stm32/                    # STM32 系列（f1/f3/f4）
│   ├── gd32/                     # GD32 系列
│   └── common/                   # 通用代码（retarget、syscalls）
│
├── cmake/
│   ├── chips/                    # 芯片配置表（每颗芯片一个 .cmake）
│   │   └── stm32f103c8.cmake    # CPU选项、编译宏、链接脚本、FreeRTOS port
│   ├── toolchain/                # 工具链文件
│   └── helpers.cmake             # CMake 辅助函数
│
├── boards/                       # 板级支持（引脚映射、时钟、FreeRTOSConfig.h）
│   ├── bluepill_f103c8/          # BluePill 板子
│   └── rm_dev_board_c/           # RoboMaster 官方 C 板
│       ├── board.cmake           # C 板 12 MHz HSE 事实
│       ├── board.h               # 引脚定义（LED、串口等）
│       └── FreeRTOSConfig.h      # 本板的 RTOS 配置
│
├── middleware/                   # 共享中间件（跨项目复用）
│   └── FreeRTOS-Kernel/          # FreeRTOS V11.1.0 LTS（源码内嵌，本地文件）
│
├── drivers/                      # 可移植设备驱动（传感器、屏幕等）
│
├── apps/                         # 应用层（每个项目一个子目录）
│   ├── blinky_f103/              # 示例：LED 闪烁 + FreeRTOS 多任务
│   └── rm_c_blinky/              # C 板安全 LED/陀螺仪验证程序
│       ├── CMakeLists.txt        # 链接 board_rm_dev_board_c
│       ├── src/main.c            # 业务逻辑
│       └── ozone/rm_c_blinky.jdebug  # Ozone/J-Link 调试项目（便携，见下文）
│
├── tools/                        # 开发工具脚本
│   ├── build_and_flash.sh        # 按目标构建并调用各 app 的 flash.jlink
│   └── run_host_tests.sh         # 主机侧固件逻辑测试
│
└── CMakeLists.txt                # 仓库根构建文件（串起整个货架）
```

## 🚀 快速开始

### 1. 克隆仓库

```bash
git clone <repo-url>
cd code
```

FreeRTOS V11.1.0 源码已内嵌在 `middleware/FreeRTOS-Kernel/`，无需额外拉取。

### 2. 安装工具链（macOS）

需要：
- arm-none-eabi-gcc（Cortex-M 交叉编译器）
- cmake + ninja（构建系统）
- J-Link（可选，用于烧录）

### 3. 编译示例项目

```bash
# 在仓库根目录
cmake -B build -G Ninja -DCMAKE_TOOLCHAIN_FILE=cmake/toolchain/arm-none-eabi-gcc.cmake
cmake --build build

# 生成固件：build/apps/blinky_f103/blinky_f103.elf/.bin/.hex

# RoboMaster C 板（STM32F407IG）安全验证固件
bash tools/build_and_flash.sh --target rm_dev_board_c --no-flash
```

### 4. 烧录到硬件（BluePill STM32F103C8）

```bash
# C 板：构建、烧录并校验（LED、TIM6 时间基、BMI088 IMU）
# 实时观测走 Ozone/J-Link（见下方"C 板硬件状态"一节），USART1 路径已废弃
bash tools/build_and_flash.sh --target rm_dev_board_c
```

### 5. 运行主机侧测试

`tests/firmware/` 是一套独立于 ARM 交叉编译链的原生主机测试工程（host GCC + CTest），用来在不接硬件的情况下验证纯逻辑（数值格式化、BMI088 陀螺仪驱动的初始化/读取/量程换算）。

```bash
bash tools/run_host_tests.sh
```

该脚本会把 `tests/firmware/` 配置为独立 CMake 项目、编译，并跑 CTest。当前 18/18 测试通过。

## 📚 分层说明

| 层级 | 路径 | 职责 | 稳定性 |
|------|------|------|--------|
| **BSP** | `bsp/` | 厂商外设库、启动文件、CMSIS | 最稳定 |
| **芯片配置** | `cmake/chips/` | 每颗芯片的编译选项、链接脚本 | 稳定 |
| **板级** | `boards/` | 引脚映射、时钟、FreeRTOSConfig.h | 中等 |
| **中间件** | `middleware/` | FreeRTOS、文件系统、协议栈 | 稳定 |
| **驱动** | `drivers/` | 可移植设备驱动（传感器、屏幕） | 中等 |
| **应用** | `apps/` | 产品业务逻辑、任务编排 | 最易变 |

**依赖方向（只能向下）**：
```
app → board → chip → bsp
app → middleware (配置来自 board)
app → drivers
```

## 🎯 如何开始新项目

### 方案 A：使用现有板子

```bash
# 1. 在 apps/ 下创建新项目目录
mkdir apps/my_project
cd apps/my_project

# 2. 编写 CMakeLists.txt，选用板子
# target_link_libraries(my_project.elf PRIVATE board_bluepill_f103c8 freertos_kernel)

# 3. 编写业务代码 src/main.c
```

### 方案 B：支持新板子

```bash
# 1. 在 boards/ 下创建板子目录
mkdir boards/my_board
cd boards/my_board

# 2. 创建 board.cmake，选用芯片
# include(${CMAKE_SOURCE_DIR}/cmake/chips/stm32f103c8.cmake)

# 3. 创建 board.h（引脚映射）、FreeRTOSConfig.h

# 4. 在 app 中选用这块板子
```

### 方案 C：支持新芯片

```bash
# 1. 在 cmake/chips/ 创建芯片配置表
# cmake/chips/stm32f407vg.cmake

# 2. 定义 CPU 选项、编译宏、启动文件、链接脚本、FreeRTOS port

# 3. 在 bsp/ 添加对应的厂商外设库

# 4. 创建板子时 include 这个芯片配置表
```

## 🔬 C 板（rm_dev_board_c）硬件状态

`apps/rm_c_blinky` 当前保留已验证的 LED 心跳和 BMI088 陀螺仪 SPI 读数，并集成 BMI088 加速度计及 IMU 数学处理路径。加速度计使用 PA4 片选；CAN、电机、PWM、ADC、DMA、EXTI 均未接入。传感器帧轴/符号映射仍未经过物理验证，不能据此推断板级方向。

### BMI088 加热实验（默认关闭）

默认 C 板构建**不会**配置 PF6 或 TIM10，也不会产生加热 PWM。只有显式传入
`-DRM_DEV_BOARD_C_ENABLE_BMI088_HEATER=ON` 的实验构建才使用官方 example 16 的
`PF6 / TIM10_CH1 / AF3`：APB2 168 MHz、PSC=0、ARR=4999、PWM1 高有效（33.6 kHz），
从 CCR=0 起步，控制输出限制为 CCR≤4500。它不是 PH10/TIM5（后者是 LED）。例如：

```bash
cmake -S . -B build-rm_dev_board_c-heater -G Ninja \
  -DCMAKE_TOOLCHAIN_FILE=cmake/toolchain/arm-none-eabi-gcc.cmake \
  -DTARGET_BOARD=rm_dev_board_c \
  -DRM_DEV_BOARD_C_ENABLE_BMI088_HEATER=ON
cmake --build build-rm_dev_board_c-heater
```

该实验以 BMI088 温度约 1 Hz 更新运行有界、按实际采样间隔积分的 PI（不复用官方
约 800 Hz 的 `Ki=0.2` 循环参数）：目标 40 °C，45 °C 截止，温度读无效或超过 2.5 s
未更新时锁存故障并将 CCR 置零。连续 10 s 落在 40±0.5 °C 后才启动并仅启动一次新的
RAM 偏置标定；Ozone 的 `heater_state`、`heater_duty`、`heater_stable_time_s` 和
`heater_fault` 是硬件验证证据，不构成性能声明。任何传感器初始化/读取、RTOS malloc、
栈溢出或任务创建失败都会强制加热输出为零。

**实时观测路径：Ozone/J-Link 是唯一文档化方式。** USART1 诊断输出路径曾经实现，但因 J-Link CDC 未接到 PA9、从未通过外接串口线抓取验证，现已废弃、不再作为观测路径维护或文档化。要在固件运行时读取 `g_gyro_snapshot`（原始/校准加速度、角速度、角加速度、四元数、欧拉角、标定和漂移指标，以及初始化/读错误状态），使用仓库提供的一键入口：

陀螺仪配置为 ±2000 dps、1000 Hz ODR / 116 Hz bandwidth，任务以 1 kHz 读取，姿态和漂移计算使用 FreeRTOS tick 差值作为实际 `dt`；温度每 1000 个成功陀螺仪样本读取一次，约为 1 Hz。启动时要求约 2 秒连续静止，用 2000 个原始计数和 `int64_t` 累加器估计偏置，避免整数 mdps 换算损失亚 mdps 均值；随后冻结偏置并自动开始 600 秒留出测量。留出期间所有有效陀螺仪样本都进入墙钟积分，运动只会令 `hold_out_valid` 变为 false，不会删除角度。陀螺仪原始证据字段继续使用 mdps，加速度原始字段使用 ug，数学字段在字段名中标出 dps、dps2、g、deg 或 deg/min。未验证传感器到板子的轴/符号映射保持不变。

```bash
# 编译 Debug 版本并直接启动 Ozone（连接 J-Link 后 Start Debug Session）
bash tools/launch_ozone.sh

# 只想编译出 .elf、不启动 Ozone：
bash tools/launch_ozone.sh --build-only

# 只校验路径与前置条件（Ozone 是否安装、.jdebug 是否存在等），不编译不启动：
bash tools/launch_ozone.sh --dry-run
```

**不要**用 `tools/build_and_flash.sh` 或裸的 `cmake --build` 来产出这个 ELF：CMake 目标默认没有 `.elf` 后缀，只有 `tools/launch_ozone.sh` 会在编译成功后把无扩展名的产物复制成 `build-rm_dev_board_c-debug/apps/rm_c_blinky/rm_c_blinky.elf`，即 Ozone 项目文件实际打开的路径。`tools/build_and_flash.sh` 仍然是烧录到硬件的正确工具，但它不生成 Ozone 需要的 `.elf` 文件名。

`rm_c_blinky.jdebug` 是随仓库提交的便携项目文件：路径都相对 `$(ProjectDir)`（即该文件所在的 `apps/rm_c_blinky/ozone/` 目录）解析，不含任何开发者本机的家目录路径，克隆到任意机器后只要跑一次 `bash tools/launch_ozone.sh` 即可直接调试，无需手动改路径。项目文件里已经用 `Window.Add` 预置了 `g_gyro_snapshot` 的各字段到 Watched Data 窗口；Ozone 的窗口布局、断点等个人调试状态保存在 Ozone 自动生成的 `.jdebug.user` 侧车文件中，不提交到仓库。

`g_gyro_snapshot` 使用 generation 协议避免调试器接受撕裂的多字段快照：先读 generation；奇数则重试；读取 payload 后再次读取 generation；仅当前后相同且为偶数时接受。在 Watched Data 窗口里同时看多个字段时，同样要先确认 `generation` 是偶数且读取前后未变，才能把这些字段当作同一时刻的一致快照。

当前观测固件自动依次进入 `CALIBRATING`、`HOLD_OUT`、`COMPLETE`。只有 `bias_frozen=1` 且 `experiment_phase` 已进入 `HOLD_OUT` 后的 `calibrated_yaw_drift_deg` 才是冻结偏置的诚实留出指标；测量期间没有 EMA、ZARU、静止吸零、死区或输出低通回灌。

同时观察：

- `temperature_valid`、`temperature_degc`、`temperature_slope_degc_per_s`：温度门是否打开。
- `sample_stationary`：当前样本是否通过静止证据判据；它与“样本已处理”是两个不同概念。
- `sample_dt_s`、`skipped_cycles`：实际周期和是否发生调度延迟。
- `experiment_phase`、`experiment_phase_elapsed_s`、`experiment_phase_required_s`：标定和 600 秒留出进度。
- `hold_out_wall_duration_s`、`hold_out_accepted_duration_s`、`hold_out_unobserved_duration_s`、`hold_out_valid`：完整墙钟窗口、静止证据和未观测间隔；运动不从角度积分中消失。
- `stack_high_water_words`：IMU 任务剩余栈高水位，必须在真实板上记录，不能只凭编译通过推断安全。
- `yaw_drift_deg_per_min`：原始静止角速度的漂移统计。
- `calibrated_yaw_drift_deg_per_min`：当前活动偏置扣除后的统计；冻结模式下才是有效留出指标。

六轴 BMI088 没有磁力计或外部航向参考，roll/pitch 可以由重力修正，但 yaw 不可观测。任何“10 分钟小于 0.1 度”的数字都必须注明是何种温度、是否冻结偏置、是否静止、是否有外部航向参考；本仓库不把它作为无辅助开环保证。

已实测证据（J-Link 会话，历史记录，部分路径已废弃见下）：

- 主机测试：18/18 通过（`bash tools/run_host_tests.sh`）。
- F103 与 C 板（`rm_dev_board_c`）交叉编译均为 clean build。
- 1 kHz 常温实验固件 `.bin`：35924 字节，SHA-256 `d74464730843dfab3b9898eb2c23c334593c2a303afb853b504cc25950b17457`。
- 烧录 + 校验：PASS。
- J-Link S/N `602712225`，VTref ≈3.28 V。
- BMI088 陀螺仪：`init_status = 0`，`read_error_count = 0`。
- 实测 generation 协议与 1 kHz 任务发布一致，`sample_dt_s=0.001`、`skipped_cycles=0`、栈高水位余量约 477 words。
- 2 秒原始计数偏置标定后的 600 秒冻结留出：Z 噪声 RMS `0.237616 dps`，残余 Z 均值 `-0.00147828 dps`，校准后 yaw `-0.882541°`；该轮因 1 dps 静止门把正常高带宽噪声尾部误判为运动而标记无效，角度仍是完整 600 秒墙钟积分。
- 静止门放宽到 2 dps 后第二轮 600 秒冻结留出：Z 噪声 RMS `0.236892 dps`，残余 Z 均值 `0.00576192 dps`，校准后 yaw `3.444227°`。600000 个样本中 61 个未通过静止判据，完整墙钟积分未删除这些样本。两轮符号和幅值差异证明 2 秒启动偏置估计不能稳定复现 0.1°/10min，也不能只挑选第一轮宣称改善。
- LED 心跳 `g_blink_count` 每 5 秒 +10（500 ms 周期），任务调度正常。
- CFSR / HFSR 均为 `0`（无 fault）。

尚未验证：物理转动方向与坐标轴符号的映射关系（即转动板子某一轴，读数是否按预期符号变化）。这项留作后续硬件门禁，当前不作为已完成项声明。

## ✨ 特性

- ✅ **多芯片支持**：STM32 F1/F3/F4、GD32（通过芯片配置表扩展）
- ✅ **FreeRTOS V11.1.0 LTS**：源码内嵌，单次编译
- ✅ **清晰分层**：芯片/板子/应用分离，依赖方向明确
- ✅ **CMake 构建**：统一构建系统，支持多项目
- ✅ **J-Link 烧录**：一键烧录脚本

## 📖 详细文档

每一层都有独立 README：

- [bsp/README.md](bsp/README.md) - BSP 层说明（如何添加厂商库）
- [cmake/chips/README.md](cmake/chips/README.md) - 芯片配置表规范
- [boards/README.md](boards/README.md) - 板级支持说明
- [middleware/README.md](middleware/README.md) - 中间件说明（FreeRTOS 配置）
- [drivers/README.md](drivers/README.md) - 驱动层说明
- [apps/README.md](apps/README.md) - 应用层说明

## 🔧 维护指南

### 添加新芯片

1. `bsp/<vendor>/<series>/` 添加厂商外设库
2. `cmake/chips/<model>.cmake` 创建芯片配置表
3. 测试：创建一个 board 引用它

### 添加新板子

1. `boards/<board_name>/` 创建目录
2. `board.cmake` 选用芯片
3. `board.h` 定义引脚映射
4. `FreeRTOSConfig.h` 配置 RTOS
5. 测试：创建一个 app 使用它

### 添加新项目

1. `apps/<project_name>/` 创建目录
2. `CMakeLists.txt` 选用板子、链接中间件/驱动
3. `src/main.c` 编写业务逻辑
4. 根 `CMakeLists.txt` 添加 `add_subdirectory(apps/<project_name>)`

## 📞 获取帮助

- 仓库结构问题：阅读各层 README
- 编译问题：检查工具链是否正确安装（`arm-none-eabi-gcc --version`）
- 烧录问题：确认 J-Link 连接、芯片型号匹配

---

**开始开发**：编译 `apps/blinky_f103` 示例项目，然后创建你的第一个应用 🚀
