# GD32/STM32 通用开发模板

支持 GD32 和 STM32 系列芯片的通用开发模板，使用统一的 BSP 层。

## 项目特点

- ✅ 支持多种芯片平台（GD32/STM32）
- ✅ 支持多个系列（F0/F1/F3/F4/F7/H7）
- ✅ Bootloader + App 双区域架构
- ✅ 统一的 BSP 层，一键切换芯片
- ✅ CMake 构建系统
- ✅ VSCode 集成

## 快速开始

### 1. 下载标准外设库

参考 `/Users/seven.xu/code/bsp/README.md`，从官网下载并安装标准外设库：

```bash
cd /Users/seven.xu/code/bsp
./download_libs.sh
```

### 2. 选择芯片平台

**方法一：使用切换脚本**
```bash
./switch_mcu.sh
```

**方法二：手动修改 CMakeLists.txt**
```cmake
set(MCU_VENDOR "gd32")      # 改成 "gd32" 或 "stm32"
set(MCU_SERIES "f4")        # 改成 "f0", "f1", "f3", "f4", "f7", "h7"
set(MCU_MODEL "GD32F407VE") # 改成具体型号
```

### 3. 编译项目

```bash
# 配置
cmake -B build -G Ninja -DCMAKE_TOOLCHAIN_FILE=cmake/arm-none-eabi-gcc.cmake

# 编译
cmake --build build

# 或在 VSCode 中按 Cmd+Shift+B
```

### 4. 烧录固件

在 VSCode 中：`Cmd+Shift+P` → Run Task → 选择烧录任务

## 支持的芯片平台

| 厂商 | 系列 | 内核 | BSP 库 | 状态 |
|------|------|------|--------|------|
| GD32 | F0 | Cortex-M0 | 标准外设库 | ✅ |
| GD32 | F1 | Cortex-M3 | 标准外设库 | ✅ |
| GD32 | F3 | Cortex-M4 | 标准外设库 | ✅ |
| GD32 | F4 | Cortex-M4 | 标准外设库 | ✅ |
| STM32 | F0 | Cortex-M0 | 标准外设库 | ✅ |
| STM32 | F1 | Cortex-M3 | 标准外设库 | ✅ |
| STM32 | F3 | Cortex-M4 | 标准外设库 | ✅ |
| STM32 | F4 | Cortex-M4 | 标准外设库 | ✅ |
| STM32 | F7 | Cortex-M7 | HAL 库 | ⚠️ 需要 HAL |
| STM32 | H7 | Cortex-M7 | HAL 库 | ⚠️ 需要 HAL |

## 项目结构

```
gd32_template/
├── CMakeLists.txt          # 主配置（在这里选择芯片）
├── switch_mcu.sh           # 一键切换芯片脚本
├── bootloader/             # Bootloader 项目
│   ├── src/main.c
│   ├── startup/
│   └── link/
├── app/                    # App 项目
│   ├── src/main.c
│   ├── startup/
│   └── link/
└── .vscode/                # VSCode 配置
```

## Flash 分区

- **Bootloader**: 0x08000000 - 0x08007FFF (32KB)
- **App**: 0x08008000 - 0x0807FFFF (480KB)

根据实际芯片 Flash 大小修改链接脚本。

## 使用标准外设库

### GD32 示例

```c
#include "gd32f4xx.h"
#include "gd32f4xx_gpio.h"
#include "gd32f4xx_rcu.h"

void led_init(void)
{
    // 使能时钟
    rcu_periph_clock_enable(RCU_GPIOA);
    
    // 配置 GPIO
    gpio_mode_set(GPIOA, GPIO_MODE_OUTPUT, GPIO_PUPD_NONE, GPIO_PIN_5);
    gpio_output_options_set(GPIOA, GPIO_OTYPE_PP, GPIO_OSPEED_50MHZ, GPIO_PIN_5);
}
```

### STM32 示例

```c
#include "stm32f4xx.h"
#include "stm32f4xx_gpio.h"
#include "stm32f4xx_rcc.h"

void led_init(void)
{
    GPIO_InitTypeDef GPIO_InitStruct;
    
    // 使能时钟
    RCC_AHB1PeriphClockCmd(RCC_AHB1Periph_GPIOA, ENABLE);
    
    // 配置 GPIO
    GPIO_InitStruct.GPIO_Pin = GPIO_Pin_5;
    GPIO_InitStruct.GPIO_Mode = GPIO_Mode_OUT;
    GPIO_InitStruct.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_InitStruct.GPIO_OType = GPIO_OType_PP;
    GPIO_InitStruct.GPIO_PuPd = GPIO_PuPd_NOPULL;
    GPIO_Init(GPIOA, &GPIO_InitStruct);
}
```

## 串口调试输出

项目已集成 printf 重定向，只需实现 `uart_send_byte()` 函数：

```c
#include <stdint.h>

// 实现这个函数
void uart_send_byte(uint8_t byte)
{
    // GD32 示例
    usart_data_transmit(USART0, byte);
    while(RESET == usart_flag_get(USART0, USART_FLAG_TBE));
    
    // 或 STM32 示例
    // USART_SendData(USART1, byte);
    // while(USART_GetFlagStatus(USART1, USART_FLAG_TXE) == RESET);
}

int main(void)
{
    // 初始化 UART...
    
    printf("Hello from %s!\n", MCU_VENDOR);
}
```

## 切换芯片平台

### 从 GD32F4 切换到 STM32F4

1. 运行切换脚本或修改 CMakeLists.txt
2. 替换启动文件和链接脚本
3. 修改代码中的头文件和 API 调用
4. 重新编译

大部分外设 API 相似，只需修改少量代码。

## 常见问题

### 编译时提示找不到标准外设库？

```
CMake Warning at CMakeLists.txt:XX (message):
  BSP 库未找到: /Users/seven.xu/code/bsp/gd32/f4/src
```

**解决方法**：参考 `/Users/seven.xu/code/bsp/README.md` 下载标准外设库。

### 如何添加新的外设？

在对应的 BSP 目录中已包含所有外设驱动，直接包含头文件即可：

```c
#include "gd32f4xx_timer.h"   // GD32
#include "stm32f4xx_tim.h"    // STM32
```

### GD32 和 STM32 API 有什么区别？

虽然大部分相似，但有一些差异：

| 功能 | GD32 | STM32 |
|------|------|-------|
| 时钟使能 | `rcu_periph_clock_enable()` | `RCC_AHBPeriphClockCmd()` |
| GPIO 配置 | `gpio_mode_set()` | `GPIO_Init()` |
| 中断配置 | `nvic_irq_enable()` | `NVIC_EnableIRQ()` |

建议参考各自的标准外设库文档。

## 下一步

- [ ] 添加更多示例代码（GPIO、UART、Timer 等）
- [ ] 添加 HAL 库支持（STM32F7/H7）
- [ ] 添加 FreeRTOS 支持
- [ ] 添加单元测试框架

## 参考资料

- [GD32 官方文档](https://www.gd32mcu.com/cn/download)
- [STM32 官方文档](https://www.st.com/en/microcontrollers-microprocessors/stm32-32-bit-arm-cortex-mcus.html)
- [BSP 层说明](../bsp/README.md)
