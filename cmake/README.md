# CMake 构建配置层

此目录集中管理所有构建配置与编译工具链。

## 目录结构

```
cmake/
├── toolchain/              # 工具链文件（arm-none-eabi-gcc.cmake）
├── chips/                  # 芯片配置表（stm32f103c8.cmake）
└── helpers.cmake           # 构建辅助函数（add_linker_script）
```

## 作用

### `toolchain/`
定义交叉编译器路径与基础配置，仅关注编译器本身，不涉及具体芯片。

### `chips/`
**单一真相源**：每个芯片一个 `.cmake` 文件，包含：
- 编译器标志（`-mcpu`, `-mfpu`, `-mfloat-abi`）
- 芯片宏定义（`STM32F103xB`, `USE_HAL_DRIVER`）
- BSP 链接（链接对应的 `bsp_stm32_f1` 库）
- 启动文件与链接脚本路径

**应用层与板级层通过 `include(cmake/chips/<model>.cmake)` 获取芯片配置，不再直接写死任何芯片相关参数。**

### `helpers.cmake`
提供构建辅助函数，如：
- `add_linker_script()`: 统一处理链接脚本添加逻辑

## 新增芯片

1. 在 `bsp/<vendor>/<series>/` 添加 HAL、启动文件、链接脚本
2. 在 `cmake/chips/<model>.cmake` 创建配置表
3. 板级或应用通过 `include(cmake/chips/<model>.cmake)` 引入

## 新增板型

板级目录（`boards/<board_name>/`）通过 `include` 对应芯片配置即可复用。
