#!/bin/bash

# 快速切换芯片平台的脚本

set -e

echo "=========================================="
echo "芯片平台切换工具"
echo "=========================================="
echo ""

PS3="请选择芯片平台: "
options=("GD32F4" "GD32F1" "GD32F3" "STM32F4" "STM32F1" "STM32F3" "退出")

select opt in "${options[@]}"
do
    case $opt in
        "GD32F4")
            MCU_VENDOR="gd32"
            MCU_SERIES="f4"
            MCU_MODEL="GD32F407VE"
            break
            ;;
        "GD32F1")
            MCU_VENDOR="gd32"
            MCU_SERIES="f1"
            MCU_MODEL="GD32F103VE"
            break
            ;;
        "GD32F3")
            MCU_VENDOR="gd32"
            MCU_SERIES="f3"
            MCU_MODEL="GD32F303VE"
            break
            ;;
        "STM32F4")
            MCU_VENDOR="stm32"
            MCU_SERIES="f4"
            MCU_MODEL="STM32F407VE"
            break
            ;;
        "STM32F1")
            MCU_VENDOR="stm32"
            MCU_SERIES="f1"
            MCU_MODEL="STM32F103VE"
            break
            ;;
        "STM32F3")
            MCU_VENDOR="stm32"
            MCU_SERIES="f3"
            MCU_MODEL="STM32F303VE"
            break
            ;;
        "退出")
            exit 0
            ;;
        *) echo "无效选项 $REPLY";;
    esac
done

echo ""
echo "切换到: $MCU_VENDOR $MCU_SERIES ($MCU_MODEL)"
echo ""

# 修改 CMakeLists.txt
sed -i.bak \
    -e "s/set(MCU_VENDOR \".*\")/set(MCU_VENDOR \"$MCU_VENDOR\")/" \
    -e "s/set(MCU_SERIES \".*\")/set(MCU_SERIES \"$MCU_SERIES\")/" \
    -e "s/set(MCU_MODEL \".*\")/set(MCU_MODEL \"$MCU_MODEL\")/" \
    CMakeLists.txt

echo "✅ CMakeLists.txt 已更新"
echo ""
echo "⚠️  注意：你可能还需要修改："
echo "   1. 启动文件: bootloader/startup/ 和 app/startup/"
echo "   2. 链接脚本: bootloader/link/ 和 app/link/"
echo "   3. J-Link 脚本中的芯片型号"
echo ""
echo "然后重新配置项目:"
echo "   rm -rf build"
echo "   cmake -B build -G Ninja -DCMAKE_TOOLCHAIN_FILE=cmake/arm-none-eabi-gcc.cmake"
echo "   cmake --build build"
