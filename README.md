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
│   └── bluepill_f103c8/          # BluePill 板子
│       ├── board.cmake           # 选用芯片：include cmake/chips/stm32f103c8.cmake
│       ├── board.h               # 引脚定义（LED、串口等）
│       └── FreeRTOSConfig.h      # 本板的 RTOS 配置
│
├── middleware/                   # 共享中间件（跨项目复用）
│   └── FreeRTOS-Kernel/          # FreeRTOS V11.1.0 LTS（源码内嵌，本地文件）
│
├── drivers/                      # 可移植设备驱动（传感器、屏幕等）
│
├── apps/                         # 应用层（每个项目一个子目录）
│   └── blinky_f103/              # 示例：LED 闪烁 + FreeRTOS 多任务
│       ├── CMakeLists.txt        # 选用板子：add_subdirectory(boards/bluepill_f103c8)
│       └── src/main.c            # 业务逻辑
│
├── tools/                        # 开发工具脚本
│   ├── setup_macos.sh            # macOS 工具链安装（brew、arm-none-eabi-gcc、ninja）
│   └── jlink/                    # J-Link 烧录脚本
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

```bash
bash tools/setup_macos.sh
```

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
```

### 4. 烧录到硬件（BluePill STM32F103C8）

```bash
# 使用 J-Link
cd tools/jlink
./flash_app.sh ../../build/apps/blinky_f103/blinky_f103.bin
```

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
