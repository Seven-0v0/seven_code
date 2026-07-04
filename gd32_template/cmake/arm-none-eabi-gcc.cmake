# ARM GCC 工具链配置文件

set(CMAKE_SYSTEM_NAME Generic)
set(CMAKE_SYSTEM_PROCESSOR ARM)

# 工具链前缀
set(TOOLCHAIN_PREFIX arm-none-eabi-)

# 查找工具链（Homebrew 安装的位置）
find_program(CMAKE_C_COMPILER ${TOOLCHAIN_PREFIX}gcc)
find_program(CMAKE_CXX_COMPILER ${TOOLCHAIN_PREFIX}g++)
find_program(CMAKE_ASM_COMPILER ${TOOLCHAIN_PREFIX}gcc)
find_program(CMAKE_AR ${TOOLCHAIN_PREFIX}ar)
find_program(CMAKE_OBJCOPY ${TOOLCHAIN_PREFIX}objcopy)
find_program(CMAKE_OBJDUMP ${TOOLCHAIN_PREFIX}objdump)
find_program(CMAKE_SIZE ${TOOLCHAIN_PREFIX}size)
find_program(CMAKE_GDB ${TOOLCHAIN_PREFIX}gdb)

# 如果找不到工具链，给出提示
if(NOT CMAKE_C_COMPILER)
    message(FATAL_ERROR "ARM GCC toolchain not found. Please install it with: brew install arm-gcc-bin")
endif()

# 编译器测试
set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)

# 搜索路径配置
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)
