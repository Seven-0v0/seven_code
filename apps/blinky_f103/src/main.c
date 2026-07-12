/**
 * STM32F103C8T6 AI Debug Firmware (FreeRTOS)
 * 时钟：HSE 8MHz → PLL x9 → 72MHz
 * 功能：3 个 FreeRTOS 任务（LED / IWDG / LOG）+ UART 串口调试输出 + IWDG 看门狗
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
#include "FreeRTOS.h"
#include "task.h"

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
/* FreeRTOS 钩子函数（configCHECK_FOR_STACK_OVERFLOW=2 等 要求）      */
/* ----------------------------------------------------------------- */
void vApplicationTickHook(void)
{
    HAL_IncTick();    /* 保持 HAL 时间基 */
    g_uptime_ms++;   /* 全局计数器 */
}

void vApplicationStackOverflowHook(TaskHandle_t xTask, char *pcTaskName)
{
    (void)xTask;
    LOG(FATAL, "rtos", "stack_overflow task=%s", pcTaskName);
    while (1);
}

void vApplicationMallocFailedHook(void)
{
    LOG(FATAL, "rtos", "malloc_failed");
    while (1);
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
/* 临界区保护：防止多任务并发输出交错                                */
/* ----------------------------------------------------------------- */
void uart_send_byte(uint8_t byte)
{
    taskENTER_CRITICAL();
    HAL_UART_Transmit(&huart1, &byte, 1, 10);
    taskEXIT_CRITICAL();
}

/* ----------------------------------------------------------------- */
/* IWDG 独立看门狗                                                   */
/* 超时 = LSI 40kHz / 64 × 4095 ≈ 6.55 秒
 * iwdg_task 每 500ms 刷新 → 余量 13 倍 */
/* ----------------------------------------------------------------- */
static void IWDG_Init(void)
{
    hiwdg.Instance = IWDG;
    hiwdg.Init.Prescaler = IWDG_PRESCALER_64;
    hiwdg.Init.Reload = 4095;
    HAL_IWDG_Init(&hiwdg);
}

/* ----------------------------------------------------------------- */
/* FreeRTOS 任务                                                      */
/* ----------------------------------------------------------------- */

/* LED 交替闪烁任务 — 500ms 周期 */
static void led_task(void *arg)
{
    (void)arg;
    uint8_t state = 0;
    for (;;) {
        if (state == 0) {
            HAL_GPIO_WritePin(GPIOA, GPIO_PIN_0, GPIO_PIN_RESET);
            HAL_GPIO_WritePin(GPIOA, GPIO_PIN_1, GPIO_PIN_SET);
        } else {
            HAL_GPIO_WritePin(GPIOA, GPIO_PIN_0, GPIO_PIN_SET);
            HAL_GPIO_WritePin(GPIOA, GPIO_PIN_1, GPIO_PIN_RESET);
        }
        LOG(OK, "led_task", "toggle state=%s", state ? "B" : "A");
        state = !state;
        vTaskDelay(pdMS_TO_TICKS(500));
    }
}

/* IWDG 看门狗刷新任务 — 500ms 周期 */
static void iwdg_task(void *arg)
{
    (void)arg;
    for (;;) {
        HAL_IWDG_Refresh(&hiwdg);
        LOG(OK, "iwdg_task", "refresh");
        vTaskDelay(pdMS_TO_TICKS(500));
    }
}

/* 日志心跳任务 — 2s 周期 */
static void log_task(void *arg)
{
    (void)arg;
    for (;;) {
        LOG(OK, "log_task", "alive uptime=%lu freertos_tick=%lu",
            g_uptime_ms, (unsigned long)xTaskGetTickCount());
        vTaskDelay(pdMS_TO_TICKS(2000));
    }
}

/* ----------------------------------------------------------------- */
/* 主函数                                                            */
/* ----------------------------------------------------------------- */
int main(void)
{
    GPIO_InitTypeDef gpio = {0};

    /* 系统初始化 */
    HAL_Init();
    SystemClock_Config();

    /* NVIC 优先级分组为 Group 4（全为抢占位）— FreeRTOS ARM_CM3 要求 */
    HAL_NVIC_SetPriorityGrouping(NVIC_PRIORITYGROUP_4);

    __enable_irq();

    /* 外设初始化 */
    UART1_Init();
    IWDG_Init();

    printf("[BOOT] STM32F103C8T6 FreeRTOS System\n");

    /* LED GPIO */
    __HAL_RCC_GPIOA_CLK_ENABLE();
    gpio.Pin = GPIO_PIN_0 | GPIO_PIN_1;
    gpio.Mode = GPIO_MODE_OUTPUT_PP;
    gpio.Pull = GPIO_NOPULL;
    gpio.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(GPIOA, &gpio);

    LOG(OK, "boot", "init_complete sysclk=%lu", HAL_RCC_GetSysClockFreq());

    /* 创建 FreeRTOS 任务 */
    xTaskCreate(led_task,  "led",  configMINIMAL_STACK_SIZE,   NULL, 2, NULL);
    xTaskCreate(iwdg_task, "iwdg", configMINIMAL_STACK_SIZE,   NULL, 3, NULL);
    xTaskCreate(log_task,  "log",  configMINIMAL_STACK_SIZE*2, NULL, 1, NULL);

    /* 启动调度器 — 不会返回 */
    vTaskStartScheduler();

    /* 调度器启动失败时才跑到这 */
    LOG(FATAL, "rtos", "scheduler_start_failed");
    while (1);
}
