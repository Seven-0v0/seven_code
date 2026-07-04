/**
 * @file verify_main.c
 * @brief GD32F4xx 验证程序：LED 闪烁 + 串口 printf + FPU 测试
 *
 * 硬件配置（根据你的开发板修改）：
 *   - LED:  PA5（高电平点亮）
 *   - UART: USART0, TX=PA9, RX=PA10, 115200 波特率
 *
 * 预期串口输出：
 *   [BOOT] GD32F4xx Verification
 *   SystemCoreClock=200000000
 *   FPU: 3.0 (expect 3.0)
 *   [LOOP] count=0
 *   [LOOP] count=1
 *   ...
 */

#include "gd32f4xx.h"
#include "gd32f4xx_gpio.h"
#include "gd32f4xx_rcu.h"
#include "gd32f4xx_usart.h"
#include <stdio.h>
#include <stdint.h>

/* ================================================================
 * 引脚配置（在这里修改你的 LED 和 UART 引脚）
 * ================================================================ */
#define LED_PORT        GPIOA
#define LED_PIN         GPIO_PIN_5      // 修改这里换 LED 引脚
#define LED_CLOCK       RCU_GPIOA

#define UART_PORT       USART0
#define UART_CLOCK      RCU_USART0
#define UART_TX_PORT    GPIOA
#define UART_TX_PIN     GPIO_PIN_9      // 修改这里换 UART TX
#define UART_RX_PORT    GPIOA
#define UART_RX_PIN     GPIO_PIN_10     // 修改这里换 UART RX
#define UART_GPIO_CLOCK RCU_GPIOA
#define UART_GPIO_AF    GPIO_AF_7
#define UART_BAUDRATE   115200           // 修改这里换波特率

/* ================================================================
 * 延时函数
 * ================================================================ */
static void delay_ms(uint32_t ms)
{
    // 简单软件延时，SystemCoreClock 约 200MHz
    for (uint32_t i = 0; i < ms; i++) {
        for (volatile uint32_t j = 0; j < 50000; j++) {
            __asm__("nop");
        }
    }
}

/* ================================================================
 * 初始化 LED（PA5，推挽输出）
 * ================================================================ */
static void led_init(void)
{
    rcu_periph_clock_enable(LED_CLOCK);
    gpio_mode_set(LED_PORT, GPIO_MODE_OUTPUT, GPIO_PUPD_NONE, LED_PIN);
    gpio_output_options_set(LED_PORT, GPIO_OTYPE_PP, GPIO_OSPEED_50MHZ, LED_PIN);
    gpio_bit_reset(LED_PORT, LED_PIN);  // 初始熄灭
}

/* ================================================================
 * 初始化 UART（USART0, PA9/PA10, 115200-8-N-1）
 * ================================================================ */
static void uart_init(void)
{
    // 使能时钟
    rcu_periph_clock_enable(UART_GPIO_CLOCK);
    rcu_periph_clock_enable(UART_CLOCK);

    // 配置 TX 引脚（PA9）为复用推挽输出
    gpio_af_set(UART_TX_PORT, UART_GPIO_AF, UART_TX_PIN);
    gpio_mode_set(UART_TX_PORT, GPIO_MODE_AF, GPIO_PUPD_PULLUP, UART_TX_PIN);
    gpio_output_options_set(UART_TX_PORT, GPIO_OTYPE_PP, GPIO_OSPEED_50MHZ, UART_TX_PIN);

    // 配置 RX 引脚（PA10）为复用输入
    gpio_af_set(UART_RX_PORT, UART_GPIO_AF, UART_RX_PIN);
    gpio_mode_set(UART_RX_PORT, GPIO_MODE_AF, GPIO_PUPD_PULLUP, UART_RX_PIN);

    // 配置 USART
    usart_deinit(UART_PORT);
    usart_baudrate_set(UART_PORT, UART_BAUDRATE);
    usart_word_length_set(UART_PORT, USART_WL_8BIT);
    usart_stop_bit_set(UART_PORT, USART_STB_1BIT);
    usart_parity_config(UART_PORT, USART_PM_NONE);
    usart_hardware_flow_rts_config(UART_PORT, USART_RTS_DISABLE);
    usart_hardware_flow_cts_config(UART_PORT, USART_CTS_DISABLE);
    usart_transmit_config(UART_PORT, USART_TRANSMIT_ENABLE);
    usart_receive_config(UART_PORT, USART_RECEIVE_DISABLE);
    usart_enable(UART_PORT);
}

/* ================================================================
 * 串口发送单个字节（供 printf 重定向使用）
 * ================================================================ */
uint8_t uart_send_byte(uint8_t byte)
{
    while (RESET == usart_flag_get(UART_PORT, USART_FLAG_TBE));
    usart_data_transmit(UART_PORT, byte);
    return byte;
}

/* ================================================================
 * 主函数
 * ================================================================ */
int main(void)
{
    uint32_t count = 0;

    // 系统初始化（时钟配置）
    SystemInit();
    SystemCoreClockUpdate();

    // 外设初始化
    led_init();
    uart_init();

    // 启动信息
    printf("\n\n[BOOT] GD32F4xx Verification\n");
    printf("SystemCoreClock=%lu Hz\n", SystemCoreClock);

    // FPU 浮点测试
    {
        volatile float a = 1.5f;
        volatile float b = 2.0f;
        volatile float result = a * b;
        printf("FPU: %.1f (expect 3.0)\n", result);

        if (result < 2.9f || result > 3.1f) {
            printf("[ERROR] FPU test failed!\n");
        }
    }

    // 主循环：LED 翻转 + 串口打印
    while (1) {
        printf("[LOOP] count=%lu\n", count++);
        gpio_bit_toggle(LED_PORT, LED_PIN);
        delay_ms(500);
    }

    return 0;
}
