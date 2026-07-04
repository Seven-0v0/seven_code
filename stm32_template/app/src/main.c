/**
 * STM32F103C8T6 AI Debug Firmware
 * 时钟：HSE 8MHz → PLL x9 → 72MHz
 * 功能：LED交替闪烁 + UART串口调试输出 + SysTick运行时间 + IWDG看门狗
 * 
 * 引脚：
 *   PA0, PA1 — LED 输出（高电平点亮）
 *   PA9 (TX), PA10 (RX) — USART1, 115200 8N1
 * 
 * AI 解析格式：[LEVEL][MODULE] key=value ...
 *   日志级别：OK, DBG, WARN, ERR, FATAL
 */

#include "stm32f1xx_hal.h"
#include "debug.h"

/* 全局变量 */
volatile uint32_t g_uptime_ms = 0;

/* 外设句柄 */
static UART_HandleTypeDef huart1;
static IWDG_HandleTypeDef hiwdg;

/* ----------------------------------------------------------------- */
/* 系统时钟配置：HSE 8MHz → PLL x9 → 72MHz                          */
/* ----------------------------------------------------------------- */
static void SystemClock_Config(void)
{
    RCC_OscInitTypeDef osc = {0};
    RCC_ClkInitTypeDef clk = {0};

    osc.OscillatorType = RCC_OSCILLATORTYPE_HSE;
    osc.HSEState = RCC_HSE_ON;
    osc.HSEPredivValue = RCC_HSE_PREDIV_DIV1;
    osc.PLL.PLLState = RCC_PLL_ON;
    osc.PLL.PLLSource = RCC_PLLSOURCE_HSE;
    osc.PLL.PLLMUL = RCC_PLL_MUL9;
    HAL_RCC_OscConfig(&osc);

    clk.ClockType = RCC_CLOCKTYPE_HCLK | RCC_CLOCKTYPE_SYSCLK
                  | RCC_CLOCKTYPE_PCLK1 | RCC_CLOCKTYPE_PCLK2;
    clk.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
    clk.AHBCLKDivider = RCC_SYSCLK_DIV1;
    clk.APB1CLKDivider = RCC_HCLK_DIV2;
    clk.APB2CLKDivider = RCC_HCLK_DIV1;
    HAL_RCC_ClockConfig(&clk, FLASH_LATENCY_2);
}

/* ----------------------------------------------------------------- */
/* SysTick 中断处理：bare metal 需要自己实现                          */
/*   启动文件默认将 SysTick_Handler 指向 Default_Handler（bx lr），  */
/*   所以必须在 main.c 中提供强符号实现来覆盖。                       */
/* ----------------------------------------------------------------- */
void SysTick_Handler(void)
{
    HAL_IncTick();    /* HAL 内部 uwTick++ */
    g_uptime_ms++;    /* 我们的全局计数器  */
}

/* ----------------------------------------------------------------- */
/* 软件延时（粗略）                                                   */
/* ----------------------------------------------------------------- */
static void delay(uint32_t n)
{
    for (volatile uint32_t i = 0; i < n; i++) __asm__("nop");
}

/* ----------------------------------------------------------------- */
/* UART1 初始化：PA9 TX, PA10 RX, 115200 8N1, 无流控                */
/* ----------------------------------------------------------------- */
static void UART1_Init(void)
{
    GPIO_InitTypeDef gpio = {0};

    /* 使能时钟 */
    __HAL_RCC_GPIOA_CLK_ENABLE();
    __HAL_RCC_USART1_CLK_ENABLE();

    /* PA9 — USART1_TX (Alternate Function Push-Pull) */
    gpio.Pin = GPIO_PIN_9;
    gpio.Mode = GPIO_MODE_AF_PP;
    gpio.Speed = GPIO_SPEED_FREQ_HIGH;
    HAL_GPIO_Init(GPIOA, &gpio);

    /* PA10 — USART1_RX (浮空输入) */
    gpio.Pin = GPIO_PIN_10;
    gpio.Mode = GPIO_MODE_INPUT;
    gpio.Pull = GPIO_NOPULL;
    HAL_GPIO_Init(GPIOA, &gpio);

    /* UART 参数 */
    huart1.Instance = USART1;
    huart1.Init.BaudRate = 115200;
    huart1.Init.WordLength = UART_WORDLENGTH_8B;
    huart1.Init.StopBits = UART_STOPBITS_1;
    huart1.Init.Parity = UART_PARITY_NONE;
    huart1.Init.Mode = UART_MODE_TX_RX;
    huart1.Init.HwFlowCtl = UART_HWCONTROL_NONE;
    huart1.Init.OverSampling = UART_OVERSAMPLING_16;
    HAL_UART_Init(&huart1);
}

/* ----------------------------------------------------------------- */
/* 强符号 uart_send_byte — 覆盖 retarget.c 中的 weak 默认实现       */
/* printf → _write → uart_send_byte → HAL_UART_Transmit              */
/* ----------------------------------------------------------------- */
void uart_send_byte(uint8_t byte)
{
    HAL_UART_Transmit(&huart1, &byte, 1, 10);
}

/* ----------------------------------------------------------------- */
/* IWDG 独立看门狗：LSI 40kHz / 64 × 4095 ≈ 6.55 秒超时             */
/* ----------------------------------------------------------------- */
static void IWDG_Init(void)
{
    hiwdg.Instance = IWDG;
    hiwdg.Init.Prescaler = IWDG_PRESCALER_64;
    hiwdg.Init.Reload = 4095;
    HAL_IWDG_Init(&hiwdg);
}

/* ----------------------------------------------------------------- */
/* 主函数                                                            */
/* ----------------------------------------------------------------- */
int main(void)
{
    GPIO_InitTypeDef gpio = {0};
    uint8_t led_state = 0;

    /* 系统初始化 */
    HAL_Init();
    SystemClock_Config();

    /* 启动文件 Reset_Handler 开头有 cpsid i（禁用全局中断）。
     * 此处显式开启，SysTick 才能触发。 */
    __enable_irq();

    /* 串口初始化（_write 在 retarget.c 中直接调用 uart_send_byte，无需额外初始化） */
    UART1_Init();

    /* 看门狗初始化 */
    IWDG_Init();

    printf("[BOOT] STM32F103C8T6 AI Debug System\n");

    /* LED 引脚初始化：PA0, PA1 推挽输出 */
    __HAL_RCC_GPIOA_CLK_ENABLE();
    gpio.Pin = GPIO_PIN_0 | GPIO_PIN_1;
    gpio.Mode = GPIO_MODE_OUTPUT_PP;
    gpio.Pull = GPIO_NOPULL;
    gpio.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(GPIOA, &gpio);

    /* 启动日志 */
    LOG(OK, "boot", "init_complete uptime_ms=%lu sysclk=%lu",
        g_uptime_ms, HAL_RCC_GetSysClockFreq());

    /* 主循环 */
    while (1) {
        /* 喂狗 */
        HAL_IWDG_Refresh(&hiwdg);

        /* LED 状态切换 */
        if (led_state == 0) {
            HAL_GPIO_WritePin(GPIOA, GPIO_PIN_0, GPIO_PIN_RESET);
            HAL_GPIO_WritePin(GPIOA, GPIO_PIN_1, GPIO_PIN_SET);
        } else {
            HAL_GPIO_WritePin(GPIOA, GPIO_PIN_0, GPIO_PIN_SET);
            HAL_GPIO_WritePin(GPIOA, GPIO_PIN_1, GPIO_PIN_RESET);
        }

        /* 状态日志（同时输出 HalTick 和 g_uptime 用于调试） */
        LOG(OK, "led", "toggle hal=%lu uptime=%lu state=%s",
            HAL_GetTick(), g_uptime_ms, led_state ? "B" : "A");
        led_state = !led_state;

        /* 延时约 1.5 秒 */
        delay(2500000);
    }

    return 0;
}
