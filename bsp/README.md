# BSP 层 - 板级支持包

通用的嵌入式 BSP 层，支持 GD32 和 STM32 系列芯片的标准外设库。

## 目录结构

```
bsp/
├── gd32/                    # GD32 系列
│   ├── f0/                  # GD32F0xx 系列
│   │   ├── src/            # 标准外设库源文件
│   │   ├── inc/            # 标准外设库头文件
│   │   ├── startup/        # 启动文件
│   │   └── CMakeLists.txt
│   ├── f1/                  # GD32F1xx 系列
│   ├── f3/                  # GD32F3xx 系列
│   ├── f4/                  # GD32F4xx 系列
│   └── ...
├── stm32/                   # STM32 系列
│   ├── f0/                  # STM32F0xx 系列
│   ├── f1/                  # STM32F1xx 系列
│   ├── f3/                  # STM32F3xx 系列
│   ├── f4/                  # STM32F4xx 系列
│   ├── f7/                  # STM32F7xx 系列
│   ├── h7/                  # STM32H7xx 系列
│   └── ...
└── common/                  # 通用代码
    ├── retarget.c          # printf 重定向
    ├── syscalls.c          # 系统调用
    └── delay.c             # 延时函数
```

## 下载标准外设库

### GD32 标准外设库

访问 **兆易创新（GigaDevice）官网**：
- 官网：https://www.gigadevice.com.cn/
- 下载中心：https://www.gd32mcu.com/cn/download

下载对应系列的固件库：
- **GD32F0xx**: GD32F0x0_Firmware_Library
- **GD32F1xx**: GD32F10x_Firmware_Library / GD32F1x0_Firmware_Library
- **GD32F3xx**: GD32F30x_Firmware_Library / GD32F3x0_Firmware_Library
- **GD32F4xx**: GD32F4xx_Firmware_Library

下载后解压，将以下内容复制到对应目录：
```bash
# 以 GD32F4xx 为例
GD32F4xx_Firmware_Library/
  ├── GD32F4xx_standard_peripheral/
  │   ├── Source/           → 复制到 bsp/gd32/f4/src/
  │   └── Include/          → 复制到 bsp/gd32/f4/inc/
  └── CMSIS/
      └── GD/
          └── GD32F4xx/
              ├── Source/Templates/  → 启动文件复制到 bsp/gd32/f4/startup/
              └── Include/           → 系统头文件复制到 bsp/gd32/f4/inc/
```

### STM32 标准外设库（Legacy）

访问 **STMicroelectronics 官网**：
- 官网：https://www.st.com/
- 下载中心：https://www.st.com/en/embedded-software/stm32-standard-peripheral-libraries.html

**注意**：ST 现在主推 HAL 库和 CubeMX，标准外设库（SPL）已停止更新，但仍可下载：

下载对应系列的固件库：
- **STM32F0xx**: STM32F0xx_StdPeriph_Lib_V1.5.0
- **STM32F1xx**: STM32F10x_StdPeriph_Lib_V3.5.0
- **STM32F3xx**: STM32F30x_StdPeriph_Lib_V1.2.3
- **STM32F4xx**: STM32F4xx_DSP_StdPeriph_Lib_V1.9.0
- **STM32F7xx**: STM32Cube_FW_F7 (使用 HAL，无 SPL)
- **STM32H7xx**: STM32Cube_FW_H7 (使用 HAL，无 SPL)

下载后解压，将以下内容复制到对应目录：
```bash
# 以 STM32F4xx 为例
STM32F4xx_DSP_StdPeriph_Lib_V1.9.0/
  ├── Libraries/
  │   ├── STM32F4xx_StdPeriph_Driver/
  │   │   ├── src/      → 复制到 bsp/stm32/f4/src/
  │   │   └── inc/      → 复制到 bsp/stm32/f4/inc/
  │   └── CMSIS/
  │       └── Device/ST/STM32F4xx/
  │           ├── Source/Templates/  → 启动文件复制到 bsp/stm32/f4/startup/
  │           └── Include/           → 系统头文件复制到 bsp/stm32/f4/inc/
```

## 使用方法

### 1. 在项目中引用 BSP

修改你的项目 CMakeLists.txt：

```cmake
# 选择芯片平台和系列
set(MCU_VENDOR "gd32")     # 或 "stm32"
set(MCU_SERIES "f4")       # f0/f1/f3/f4/f7/h7

# 添加 BSP 子目录
add_subdirectory(/Users/seven.xu/code/bsp/${MCU_VENDOR}/${MCU_SERIES} bsp)

# 链接 BSP 库
target_link_libraries(your_app.elf PRIVATE bsp_${MCU_VENDOR}_${MCU_SERIES})
```

### 2. 在代码中使用

```c
// GD32F4xx
#include "gd32f4xx.h"
#include "gd32f4xx_gpio.h"
#include "gd32f4xx_rcu.h"

// STM32F4xx
#include "stm32f4xx.h"
#include "stm32f4xx_gpio.h"
#include "stm32f4xx_rcc.h"
```

## 配置说明

每个系列的配置文件：
- `xxx_conf.h` - 外设库配置文件（选择需要的外设模块）
- `system_xxx.c/.h` - 系统初始化和时钟配置

## 自动化下载脚本

我提供了下载脚本 `download_libs.sh`，可以自动下载和解压：

```bash
cd /Users/seven.xu/code/bsp
./download_libs.sh
```

## 常见问题

### Q: GD32 和 STM32 的外设库可以互换吗？
A: 大部分 API 相似但不完全相同。GD32 是兼容 STM32 的，但建议使用对应的库。

### Q: 为什么 STM32F7/H7 没有标准外设库？
A: ST 从 F7 开始只提供 HAL 库，不再维护标准外设库（SPL）。

### Q: 如何在 GD32 和 STM32 之间切换？
A: 修改 CMakeLists.txt 中的 `MCU_VENDOR` 和 `MCU_SERIES` 即可。

## 许可证

各厂商的标准外设库遵循各自的许可协议：
- GD32: 请参考兆易创新的许可协议
- STM32: 请参考 ST 的许可协议（通常为 BSD 3-Clause）

**注意**: 这些库仅供学习和商业开发使用，请遵守相应的许可协议。
