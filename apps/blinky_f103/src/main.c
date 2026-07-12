/**
 * STM32F103C8T6 AI Debug Firmware (FreeRTOS)
 * 时钟：HSE 8MHz → PLL x9 → 72MHz
 * 功能：3 个 FreeRTOS 任务（LED 呼吸灯 / IWDG / LOG）+ UART 串口调试输出 + IWDG 看门狗
 *
 * 引脚：
 *   PA0 — LED0，TIM2_CH1 PWM 输出（呼吸灯）
 *   PA1 — LED1，TIM2_CH2 PWM 输出（呼吸灯，与 LED0 交替）
 *   PA9 (TX), PA10 (RX) — USART1, 115200 8N1
 *
 * 呼吸灯效果：LED0 渐亮→渐暗 的同时 LED1 渐暗→渐亮，周期 4 秒，相位差 180°。
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
static TIM_HandleTypeDef htim2;

/* PWM 呼吸灯参数 */
#define PWM_PERIOD        999   /* ARR：PWM 分辨率 0..999 */
#define PWM_STEPS         100   /* 呼吸灯亮度等级数 */
#define BREATH_PERIOD_MS  4000  /* 一次完整呼吸周期 4 秒 */
#define BREATH_STEP_MS    (BREATH_PERIOD_MS / (PWM_STEPS * 2))  /* 每步 20ms */

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
/* TIM2 PWM 初始化：PA0=CH1, PA1=CH2，72MHz / 72 = 1MHz, ARR=999 → 1kHz */
/* ----------------------------------------------------------------- */
static void PWM_Init(void)
{
    GPIO_InitTypeDef gpio = {0};
    TIM_OC_InitTypeDef oc = {0};

    __HAL_RCC_GPIOA_CLK_ENABLE();
    __HAL_RCC_TIM2_CLK_ENABLE();

    /* PA0/PA1 复用推挽输出（TIM2_CH1/CH2） */
    gpio.Pin = GPIO_PIN_0 | GPIO_PIN_1;
    gpio.Mode = GPIO_MODE_AF_PP;
    gpio.Speed = GPIO_SPEED_FREQ_HIGH;
    HAL_GPIO_Init(GPIOA, &gpio);

    /* TIM2 基础配置：72MHz / 72 = 1MHz 计数，ARR=999 → PWM 1kHz */
    htim2.Instance = TIM2;
    htim2.Init.Prescaler = 71;
    htim2.Init.CounterMode = TIM_COUNTERMODE_UP;
    htim2.Init.Period = PWM_PERIOD;
    htim2.Init.ClockDivision = TIM_CLOCKDIVISION_DIV1;
    htim2.Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_ENABLE;
    HAL_TIM_Base_Init(&htim2);

    /* PWM 模式 1：CNT < CCR 时输出有效 */
    oc.OCMode = TIM_OCMODE_PWM1;
    oc.Pulse = 0;
    oc.OCPolarity = TIM_OCPOLARITY_HIGH;
    oc.OCFastMode = TIM_OCFAST_DISABLE;
    HAL_TIM_PWM_ConfigChannel(&htim2, &oc, TIM_CHANNEL_1);
    HAL_TIM_PWM_ConfigChannel(&htim2, &oc, TIM_CHANNEL_2);

    HAL_TIM_PWM_Start(&htim2, TIM_CHANNEL_1);
    HAL_TIM_PWM_Start(&htim2, TIM_CHANNEL_2);
}

/* ----------------------------------------------------------------- */
/* FreeRTOS 任务                                                      */
/* ----------------------------------------------------------------- */

/* LED 呼吸灯任务 — 4 秒周期，PA0/PA1 交替呼吸（相位差 180°） */
static void led_task(void *arg)
{
    (void)arg;
    uint32_t step = 0;
    for (;;) {
        /* step 在 0..(2*PWM_STEPS-1) 间循环：前半周期 LED0 渐亮、LED1 渐暗；
           后半周期 LED0 渐暗、LED1 渐亮。三角波亮度。 */
        uint32_t brightness;
        if (step < PWM_STEPS) {
            brightness = step;                       /* 0 → 99 */
        } else {
            brightness = (2 * PWM_STEPS - 1) - step; /* 99 → 0 */
        }
        /* 用 (PWM_PERIOD * brightness / PWM_STEPS) 而非 (brightness * (PWM_PERIOD/PWM_STEPS))
           避免整数除法截断：前者最大 = 999*99/100 = 989，再补 +9 让满量程到 999 */
        uint32_t duty = (uint32_t)((uint64_t)PWM_PERIOD * brightness / PWM_STEPS);
        if (brightness == PWM_STEPS - 1) duty = PWM_PERIOD;  /* 顶点拉满到 999 */

        __HAL_TIM_SET_COMPARE(&htim2, TIM_CHANNEL_1, duty);
        __HAL_TIM_SET_COMPARE(&htim2, TIM_CHANNEL_2, PWM_PERIOD - duty);  /* 反相 */

        /* 每 10 步打一次日志，避免串口阻塞拖慢呼吸周期 */
        if (step % 10 == 0) {
            LOG(OK, "led_task", "breath step=%lu duty0=%lu duty1=%lu",
                (unsigned long)step, (unsigned long)duty,
                (unsigned long)(PWM_PERIOD - duty));
        }

        step = (step + 1) % (2 * PWM_STEPS);
        vTaskDelay(pdMS_TO_TICKS(BREATH_STEP_MS));
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
    /* 系统初始化 */
    HAL_Init();
    SystemClock_Config();

    /* NVIC 优先级分组为 Group 4（全为抢占位）— FreeRTOS ARM_CM3 要求 */
    HAL_NVIC_SetPriorityGrouping(NVIC_PRIORITYGROUP_4);

    __enable_irq();

    /* 外设初始化 */
    UART1_Init();
    IWDG_Init();
    PWM_Init();

    printf("[BOOT] STM32F103C8T6 FreeRTOS Breath LED System\n");

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
