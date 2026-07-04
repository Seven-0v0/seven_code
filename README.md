# Mac 嵌入式开发环境

这个目录包含了在 Mac 上进行嵌入式开发的所有必要文件和脚本。

## 📁 目录结构

```
/Users/seven.xu/code/
├── bsp/                           # BSP 层（板级支持包）
│   ├── gd32/                     # GD32 系列外设库
│   ├── stm32/                    # STM32 系列外设库
│   └── common/                   # 通用代码
├── gd32_template/                # 项目模板（Bootloader + App）
├── install_dev_tools.sh          # 安装开发工具脚本 ⭐
├── install_firmware_libs.sh      # 安装外设库脚本
├── install_jlink.sh              # 安装 J-Link 脚本
├── install_all.sh                # 一键安装全部
├── QUICK_START.md                # 快速开始指南 ⭐ 从这里开始！
└── MAC_EMBEDDED_SETUP.md         # 完整安装指南

```

## 🚀 快速开始（3 步）

### 1. 安装开发工具（需要密码）

```bash
bash /Users/seven.xu/code/install_dev_tools.sh
```

### 2. 安装外设库

```bash
# STM32（自动）
bash /Users/seven.xu/code/install_firmware_libs.sh

# GD32（手动下载）
# 从 https://www.gd32mcu.com/cn/download 下载后按脚本提示复制
```

### 3. 测试编译

```bash
cd /Users/seven.xu/code/gd32_template
cmake -B build -G Ninja -DCMAKE_TOOLCHAIN_FILE=cmake/arm-none-eabi-gcc.cmake
cmake --build build
```

## 📖 详细文档

- **[QUICK_START.md](QUICK_START.md)** - 快速安装指南（推荐从这里开始）
- **[MAC_EMBEDDED_SETUP.md](MAC_EMBEDDED_SETUP.md)** - 完整的使用文档
- **[bsp/README.md](bsp/README.md)** - BSP 层说明
- **[gd32_template/README.md](gd32_template/README.md)** - 项目模板使用说明

## ✨ 特性

- ✅ 支持 GD32 和 STM32 多个系列
- ✅ 统一的 BSP 层，一键切换芯片
- ✅ Bootloader + App 双区域架构
- ✅ CMake 构建系统
- ✅ VSCode 完整集成
- ✅ J-Link 烧录支持

## 🎯 支持的芯片

| 厂商 | 系列 | 状态 |
|------|------|------|
| GD32 | F0/F1/F3/F4 | ✅ |
| STM32 | F0/F1/F3/F4 | ✅ |
| STM32 | F7/H7 | ⚠️ 需要 HAL 库 |

## 🔧 一键安装

```bash
bash /Users/seven.xu/code/install_all.sh
```

这会依次完成所有安装步骤（需要输入密码）。

## 📞 获取帮助

遇到问题？查看：
- 故障排除：[MAC_EMBEDDED_SETUP.md](MAC_EMBEDDED_SETUP.md#-常见问题)
- GD32 官网：https://www.gd32mcu.com/
- STM32 官网：https://www.st.com/

---

**立即开始**：查看 [QUICK_START.md](QUICK_START.md) 👈
