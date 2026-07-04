/**
 * @file main.c
 * @brief Bootloader 主程序
 */

#include <stdint.h>

#define APP_START_ADDRESS  0x08008000  // App 起始地址（32KB bootloader）

typedef void (*pFunction)(void);

/**
 * @brief 跳转到应用程序
 */
void jump_to_app(void)
{
    uint32_t app_stack = *((volatile uint32_t*)APP_START_ADDRESS);
    uint32_t app_entry = *((volatile uint32_t*)(APP_START_ADDRESS + 4));

    // 检查栈顶地址是否合法
    if ((app_stack & 0x2FFE0000) == 0x20000000) {
        pFunction jump = (pFunction)app_entry;

        // 关闭中断
        __disable_irq();

        // 设置栈顶
        __set_MSP(app_stack);

        // 跳转到应用程序
        jump();
    }
}

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
    // Bootloader 初始化
    // TODO: 添加 LED、串口等初始化

    // 检查是否需要升级
    // TODO: 添加升级逻辑判断

    // 延时，给调试时间
    delay(1000000);

    // 跳转到 App
    jump_to_app();

    // 如果跳转失败，停在这里
    while(1) {
        delay(1000000);
    }

    return 0;
}
