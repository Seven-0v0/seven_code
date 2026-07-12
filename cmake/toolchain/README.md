# cmake/toolchain/ — 交叉编译工具链定义

## 这一层放什么

只描述**编译器本身**的工具链文件（`CMAKE_TOOLCHAIN_FILE` 用的那种），例如：

- `arm-none-eabi-gcc.cmake` — 指定 `arm-none-eabi-gcc / g++ / objcopy / size` 等交叉编译器可执行文件路径，设置 `CMAKE_SYSTEM_NAME=Generic`、`CMAKE_TRY_COMPILE_TARGET_TYPE=STATIC_LIBRARY` 等与"如何调用编译器"相关的配置。

## 什么该放这里

- 编译器/汇编器/链接器可执行文件的定位与探测。
- `CMAKE_SYSTEM_NAME`、`CMAKE_SYSTEM_PROCESSOR` 等与宿主/目标系统三元组相关的通用设置。
- 与具体芯片**无关**的编译器级默认行为（例如 `-ffreestanding`、裸机 try-compile 策略）。

## 什么不该放这里

- **禁止**出现任何芯片信息：不要写 `-mcpu=cortex-m3`、启动文件、链接脚本、FreeRTOS 移植、烧录型号。这些属于 `cmake/chips/<model>.cmake`。
- 不要写板级引脚/时钟（属于 `boards/`）。
- 不要写应用逻辑。

## 命名约定

- 一个工具链一个文件：`<triplet>-<compiler>.cmake`，如 `arm-none-eabi-gcc.cmake`。

## 与相邻层的关系（依赖方向）

工具链是最底层、最通用、最稳定的一层。它只回答"用哪个编译器、怎么调它"。
`cmake/chips/<model>.cmake` 在其之上叠加芯片专属的 CPU 选项与编译宏；`boards/` 再在芯片之上叠加板级配置。
依赖方向：`app → board → chip → bsp`，工具链正交地被最外层 configure 时通过 `-DCMAKE_TOOLCHAIN_FILE=` 注入。
