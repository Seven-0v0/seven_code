/**
 * @file main.c
 * @brief App 主程序
 */

#include <stdint.h>

/**
 * @brief 延时函数
 */
void delay(volatile uint32_t count)
{
    while(count--);
}

/**
 * @brief 主函数
 */
int main(void)
{
    // 应用程序初始化
    // TODO: 添加外设初始化

    while(1) {
        // 主循环
        delay(1000000);
    }

    return 0;
}
