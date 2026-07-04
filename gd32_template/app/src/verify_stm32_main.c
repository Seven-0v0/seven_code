/**
 * @file verify_main.c
 * @brief STM32F4 验证程序：LED 闪烁 + 串口 printf + FPU 测试
 *
 * 硬件配置（根据你的开发板修改）：
 *   - LED:  PA5
 *   - UART: USART2, TX=PA2, RX=PA3, 115200
 *
 * 预期串口输出：
 *   [BOOT] STM32F4 Verification
 *   SystemCoreClock=168000000
 *   FPU: 3.0 (expect 3.0)
 *   [LOOP] count=0
 *   ...
 */

#include "stm32f4xx_hal.h"
#include <stdio.h>
#include <stdint.h>

/* ================================================================
 * 引脚配置（在这里修改 LED 和 UART 引脚）
 * ================================================================ */
#define LED_PORT        GPIOA
#define LED_PIN         GPIO_PIN_5
#define LED_CLK_ENABLE  __HAL_RCC_GPIOA_CLK_ENABLE

#define UART_INSTANCE   USART2
#define UART_BAUDRATE   115200
#define UART_TX_PORT    GPIOA
#define UART_TX_PIN     GPIO_PIN_2
#define UART_RX_PORT    GPIOA
#define UART_RX_PIN     GPIO_PIN_3
#define UART_CLK_ENABLE __HAL_RCC_USART2_CLK_ENABLE
#define UART_GPIO_CLK   __HAL_RCC_GPIOA_CLK_ENABLE
#define UART_GPIO_AF    GPIO_AF7_USART2

UART_HandleTypeDef huart2;

/* ================================================================
 * 系统时钟配置：168MHz（HSE 8MHz -> PLL -> 168MHz）
 * ================================================================ */
static void SystemClock_Config(void)
{
    RCC_OscInitTypeDef RCC_OscInitStruct = {0};
    RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};

    __HAL_RCC_PWR_CLK_ENABLE();
    __HAL_PWR_VOLTAGESCALING_CONFIG(PWR_REGULATOR_VOLTAGE_SCALE1);

    RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSE;
    RCC_OscInitStruct.HSEState = RCC_HSE_ON;
    RCC_OscInitStruct.PLL.PLLState = RCC_PLL_ON;
    RCC_OscInitStruct.PLL.PLLSource = RCC_PLLSOURCE_HSE;
    RCC_OscInitStruct.PLL.PLLM = 8;
    RCC_OscInitStruct.PLL.PLLN = 336;
    RCC_OscInitStruct.PLL.PLLP = RCC_PLLP_DIV2;
    RCC_OscInitStruct.PLL.PLLQ = 7;
    HAL_RCC_OscConfig(&RCC_OscInitStruct);

    RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK | RCC_CLOCKTYPE_SYSCLK
                                | RCC_CLOCKTYPE_PCLK1 | RCC_CLOCKTYPE_PCLK2;
    RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
    RCC_ClkInitStruct.AHBCLKDivider = RCC_SYSCLK_DIV1;
    RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV4;
    RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV2;
    HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_5);
}

/* ================================================================
 * LED 初始化
 * ================================================================ */
static void led_init(void)
{
    GPIO_InitTypeDef GPIO_InitStruct = {0};
    LED_CLK_ENABLE();
    GPIO_InitStruct.Pin = LED_PIN;
    GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
    GPIO_InitStruct.Pull = GPIO_NOPULL;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(LED_PORT, &GPIO_InitStruct);
    HAL_GPIO_WritePin(LED_PORT, LED_PIN, GPIO_PIN_RESET);
}

/* ================================================================
 * UART 初始化（USART2, PA2/PA3）
 * ================================================================ */
static void uart_init(void)
{
    GPIO_InitTypeDef GPIO_InitStruct = {0};

    UART_GPIO_CLK();
    UART_CLK_ENABLE();

    // TX 引脚
    GPIO_InitStruct.Pin = UART_TX_PIN;
    GPIO_InitStruct.Mode = GPIO_MODE_AF_PP;
    GPIO_InitStruct.Pull = GPIO_PULLUP;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
    GPIO_InitStruct.Alternate = UART_GPIO_AF;
    HAL_GPIO_Init(UART_TX_PORT, &GPIO_InitStruct);

    // RX 引脚
    GPIO_InitStruct.Pin = UART_RX_PIN;
    HAL_GPIO_Init(UART_RX_PORT, &GPIO_InitStruct);

    // UART 配置
    huart2.Instance = UART_INSTANCE;
    huart2.Init.BaudRate = UART_BAUDRATE;
    huart2.Init.WordLength = UART_WORDLENGTH_8B;
    huart2.Init.StopBits = UART_STOPBITS_1;
    huart2.Init.Parity = UART_PARITY_NONE;
    huart2.Init.Mode = UART_MODE_TX_RX;
    huart2.Init.HwFlowCtl = UART_HWCONTROL_NONE;
    huart2.Init.OverSampling = UART_OVERSAMPLING_16;
    HAL_UART_Init(&huart2);
}

/* ================================================================
 * 串口发送字节（供 printf 重定向）
 * ================================================================ */
uint8_t uart_send_byte(uint8_t byte)
{
    while (__HAL_UART_GET_FLAG(&huart2, UART_FLAG_TXE) == RESET);
    huart2.Instance->DR = byte;
    return byte;
}

/* ================================================================
 * 简单延时
 * ================================================================ */
static void delay_ms(uint32_t ms)
{
    HAL_Delay(ms);
}

/* ================================================================
 * 主函数
 * ================================================================ */
int main(void)
{
    uint32_t count = 0;

    HAL_Init();
    SystemClock_Config();
    led_init();
    uart_init();

    printf("\n\n[BOOT] STM32F4 Verification\n");
    printf("SystemCoreClock=%lu Hz\n", SystemCoreClock);

    // FPU 测试
    {
        volatile float a = 1.5f;
        volatile float b = 2.0f;
        volatile float result = a * b;
        printf("FPU: %.1f (expect 3.0)\n", result);
        if (result < 2.9f || result > 3.1f) {
            printf("[ERROR] FPU test failed!\n");
        }
    }

    while (1) {
        printf("[LOOP] count=%lu\n", count++);
        HAL_GPIO_TogglePin(LED_PORT, LED_PIN);
        HAL_Delay(500);
    }

    return 0;
}
