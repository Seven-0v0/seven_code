#!/bin/bash

# BSP 库下载脚本
# 这个脚本提供下载链接和基本指导，需要手动下载

set -e

echo "=========================================="
echo "BSP 标准外设库下载指南"
echo "=========================================="
echo ""

# 颜色定义
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

echo -e "${YELLOW}注意：由于版权限制，需要从官网手动下载标准外设库${NC}"
echo ""

echo "=========================================="
echo "GD32 标准外设库下载"
echo "=========================================="
echo ""
echo "1. 访问兆易创新官网下载中心："
echo "   https://www.gd32mcu.com/cn/download"
echo ""
echo "2. 下载以下固件库："
echo "   - GD32F0xx: GD32F0x0 固件库"
echo "   - GD32F1xx: GD32F10x 固件库"
echo "   - GD32F3xx: GD32F30x 固件库"
echo "   - GD32F4xx: GD32F4xx 固件库"
echo ""
echo "3. 下载后解压到临时目录，然后运行："
echo "   ./install_gd32.sh <解压路径> <系列>"
echo ""

echo "=========================================="
echo "STM32 标准外设库下载"
echo "=========================================="
echo ""
echo "1. 访问 ST 官网："
echo "   https://www.st.com/en/embedded-software/stm32-standard-peripheral-libraries.html"
echo ""
echo "2. 下载以下固件库："
echo "   - STM32F0xx: STM32F0xx Standard Peripherals Library"
echo "   - STM32F1xx: STM32F10x Standard Peripherals Library"
echo "   - STM32F3xx: STM32F30x Standard Peripherals Library"
echo "   - STM32F4xx: STM32F4xx DSP and Standard Peripherals Library"
echo ""
echo "   ${YELLOW}注意：F7/H7 系列使用 HAL 库，需从 STM32CubeMX 获取${NC}"
echo ""
echo "3. 下载后解压到临时目录，然后运行："
echo "   ./install_stm32.sh <解压路径> <系列>"
echo ""

echo "=========================================="
echo "或者使用 Git 克隆（非官方镜像）"
echo "=========================================="
echo ""
echo -e "${RED}警告：以下是第三方镜像，请谨慎使用！${NC}"
echo ""
echo "# STM32 标准外设库（GitHub 镜像）"
echo "git clone https://github.com/STMicroelectronics/STM32CubeF4.git"
echo ""
echo "# GD32 标准外设库（第三方镜像，非官方）"
echo "# 建议直接从官网下载"
echo ""

echo "=========================================="
echo "完成后的目录结构"
echo "=========================================="
echo ""
cat << 'EOF'
bsp/
├── gd32/
│   └── f4/
│       ├── src/           # gd32f4xx_gpio.c, gd32f4xx_rcu.c ...
│       ├── inc/           # gd32f4xx_gpio.h, gd32f4xx_rcu.h ...
│       ├── startup/       # startup_gd32f4xx.s
│       └── CMakeLists.txt
└── stm32/
    └── f4/
        ├── src/           # stm32f4xx_gpio.c, stm32f4xx_rcc.c ...
        ├── inc/           # stm32f4xx_gpio.h, stm32f4xx_rcc.h ...
        ├── startup/       # startup_stm32f4xx.s
        └── CMakeLists.txt
EOF

echo ""
echo -e "${GREEN}下载完成后，参考 README.md 进行配置${NC}"
