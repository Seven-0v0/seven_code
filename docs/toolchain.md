# Mac 嵌入式开发工具链（STM32/GD32）

## 环境

| 工具 | 版本 | 安装方式 |
|------|------|---------|
| ARM GCC | 15.3.1 | `brew install arm-gcc-bin` |
| CMake | 4.3.4 | `brew install cmake` |
| Ninja | 1.13.2 | `brew install ninja` |
| J-Link | V9.56 | 官网下载 .pkg |
| GitHub CLI | 2.96.0 | `brew install gh` |

验证安装：
```bash
arm-none-eabi-gcc --version
cmake --version
ninja --version
ls /Applications/SEGGER/JLink/JLinkExe
```

## 项目结构

```
/Users/seven.xu/code/
├── stm32_template/          # 项目模板（可复制复用）
│   ├── CMakeLists.txt       # 主构建配置（选芯片）
│   ├── cmake/               # ARM GCC 工具链配置
│   ├── app/
│   │   ├── src/             # 你的代码放这里
│   │   ├── startup/         # 启动文件（已修复）
│   │   ├── link/            # 链接脚本
│   │   └── CMakeLists.txt
│   └── flash_app.jlink      # J-Link 烧录脚本
├── bsp/                     # 芯片厂商 HAL 库
│   ├── stm32/f1/            # STM32F1 HAL（Cortex-M3）
│   ├── stm32/f4/            # STM32F4 HAL（Cortex-M4+FPU）
│   └── common/              # 通用代码（printf重定向等）
└── docs/                    # 知识库
    ├── startup-fix.md       # 启动文件修复指南
    └── toolchain.md         # 本文档
```

## 编译流程

```bash
cd /Users/seven.xu/code/stm32_template

# 配置（只需一次，除非改了 CMakeLists.txt）
cmake -B build -G Ninja -DCMAKE_TOOLCHAIN_FILE=cmake/arm-none-eabi-gcc.cmake

# 编译（每次改代码后）
cmake --build build
```

编译产物在 `build/app/`：
- `app.elf` — 可执行文件（带调试信息）
- `app.bin` — 纯二进制（烧录用）
- `app.hex` — Intel HEX 格式

## 烧录流程

**硬件连接：**
```
J-Link              STM32 板子
VTref (1)  ───→  3.3V
GND  (4)   ───→  GND
SWDIO(7)   ───→  PA13/SWDIO
SWCLK(9)   ───→  PA14/SWCLK
```

**烧录命令：**
```bash
cd /Users/seven.xu/code/stm32_template
JLinkExe -device STM32F103C8 -if SWD -speed 4000 -autoconnect 1 -CommandFile flash_app.jlink
```

**闪灯接线（验证用）：**
```
PA0 ──→ LED正极（长脚）──→ LED负极（短脚）──→ 220Ω电阻 ──→ GND
PA1 ──→ LED正极（长脚）──→ LED负极（短脚）──→ 220Ω电阻 ──→ GND
```

## 芯片配置

在 `CMakeLists.txt` 里改这三行切换芯片：

```cmake
set(MCU_VENDOR "stm32")      # "stm32" 或 "gd32"
set(MCU_SERIES "f1")         # "f0" "f1" "f3" "f4" "f7" "h7"
set(MCU_MODEL "STM32F103C8") # 具体型号
```

## Git 仓库

远程地址：https://github.com/Seven-0v0/seven_code

```bash
git add -A
git commit -m "描述改动"
git push origin main
```

`build/` 目录和 `.omo/` 已被 .gitignore 排除。

## 关键经验

1. **BOOT0 必须接 GND**，否则芯片从系统 bootloader 启动，不跑 Flash 里的程序
2. **启动文件必须修复**（见 startup-fix.md），这是 bare metal + newlib-nano 的经典坑
3. **F103 用 Cortex-M3 无 FPU**，编译标志是 `-mcpu=cortex-m3 -mthumb`
4. **F4 用 Cortex-M4 + FPU**，编译标志是 `-mcpu=cortex-m4 -mthumb -mfpu=fpv4-sp-d16 -mfloat-abi=hard`
5. **HAL 配置文件**需要启用对应模块（如 HAL_UART_MODULE_ENABLED）
6. **STM32CubeFx ZIP 下载不包含子模块**，需要单独下载 hal_driver 和 cmsis-device
