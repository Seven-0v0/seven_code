#include "board.h"

static TIM_HandleTypeDef hal_timebase;

static void halt_on_hal_error(HAL_StatusTypeDef status)
{
    if (status != HAL_OK) {
        board_imu_heater_force_off();
        for (;;) {
        }
    }
}

HAL_StatusTypeDef HAL_InitTick(uint32_t priority)
{
    const uint32_t pclk = HAL_RCC_GetPCLK1Freq();
    const uint32_t timer_clock =
        (RCC->CFGR & RCC_CFGR_PPRE1) == RCC_HCLK_DIV1 ? pclk : pclk * 2U;

    __HAL_RCC_TIM6_CLK_ENABLE();
    hal_timebase.Instance = TIM6;
    hal_timebase.Init.Prescaler = timer_clock / 1000000U - 1U;
    hal_timebase.Init.CounterMode = TIM_COUNTERMODE_UP;
    hal_timebase.Init.Period = 1000U - 1U;
    hal_timebase.Init.ClockDivision = TIM_CLOCKDIVISION_DIV1;
    hal_timebase.Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_DISABLE;

    if (HAL_TIM_Base_Init(&hal_timebase) != HAL_OK) {
        return HAL_ERROR;
    }
    HAL_NVIC_SetPriority(TIM6_DAC_IRQn, priority, 0U);
    HAL_NVIC_EnableIRQ(TIM6_DAC_IRQn);
    return HAL_TIM_Base_Start_IT(&hal_timebase);
}

void TIM6_DAC_IRQHandler(void)
{
    HAL_TIM_IRQHandler(&hal_timebase);
}

void HAL_TIM_PeriodElapsedCallback(TIM_HandleTypeDef *timer)
{
    if (timer->Instance == TIM6) {
        HAL_IncTick();
    }
}

void board_clock_init(void)
{
    RCC_OscInitTypeDef oscillator = {0};
    RCC_ClkInitTypeDef clocks = {0};

    __HAL_RCC_PWR_CLK_ENABLE();
    __HAL_PWR_VOLTAGESCALING_CONFIG(PWR_REGULATOR_VOLTAGE_SCALE1);

    oscillator.OscillatorType = RCC_OSCILLATORTYPE_HSE;
    oscillator.HSEState = RCC_HSE_ON;
    oscillator.PLL.PLLState = RCC_PLL_ON;
    oscillator.PLL.PLLSource = RCC_PLLSOURCE_HSE;
    oscillator.PLL.PLLM = 6;
    oscillator.PLL.PLLN = 168;
    oscillator.PLL.PLLP = RCC_PLLP_DIV2;
    oscillator.PLL.PLLQ = 7;
    halt_on_hal_error(HAL_RCC_OscConfig(&oscillator));

    clocks.ClockType = RCC_CLOCKTYPE_HCLK | RCC_CLOCKTYPE_SYSCLK |
                       RCC_CLOCKTYPE_PCLK1 | RCC_CLOCKTYPE_PCLK2;
    clocks.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
    clocks.AHBCLKDivider = RCC_SYSCLK_DIV1;
    clocks.APB1CLKDivider = RCC_HCLK_DIV4;
    clocks.APB2CLKDivider = RCC_HCLK_DIV2;
    halt_on_hal_error(HAL_RCC_ClockConfig(&clocks, FLASH_LATENCY_5));
}

void board_led_init(void)
{
    GPIO_InitTypeDef pin = {0};

    __HAL_RCC_GPIOH_CLK_ENABLE();
    HAL_GPIO_WritePin(BOARD_LED_PORT, BOARD_LED_PIN, GPIO_PIN_RESET);
    pin.Pin = BOARD_LED_PIN;
    pin.Mode = GPIO_MODE_OUTPUT_PP;
    pin.Pull = GPIO_PULLUP;
    pin.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(BOARD_LED_PORT, &pin);
}

void board_led_toggle(void)
{
    HAL_GPIO_TogglePin(BOARD_LED_PORT, BOARD_LED_PIN);
}

void board_led_set(uint32_t state)
{
    HAL_GPIO_WritePin(BOARD_LED_PORT, BOARD_LED_PIN,
                      state == 0U ? GPIO_PIN_RESET : GPIO_PIN_SET);
}
