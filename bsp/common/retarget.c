/**
 * @file retarget.c
 * @brief printf 重定向到 UART
 *
 * 使用方法：
 * 1. 在 main.c 中调用 retarget_init(UART_ID)
 * 2. 实现 uart_send_byte() 函数
 */

#include <stdio.h>
#include <stdint.h>

// 弱定义的 UART 发送函数，需要用户实现
__attribute__((weak)) void uart_send_byte(uint8_t byte)
{
    // 默认实现：什么都不做
    // 用户需要在项目中实现这个函数
    (void)byte;
}

/**
 * @brief 重定向 _write 系统调用（用于 printf）
 */
int _write(int file, char *ptr, int len)
{
    (void)file;

    for (int i = 0; i < len; i++) {
        uart_send_byte((uint8_t)ptr[i]);
    }

    return len;
}

/**
 * @brief 初始化重定向（可选）
 */
void retarget_init(void)
{
    // 可以在这里初始化 UART
    // 或者在 main.c 中单独初始化
}
