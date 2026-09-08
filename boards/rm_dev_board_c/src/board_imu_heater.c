#include "board.h"

#if BOARD_RM_DEV_BOARD_C_BMI088_HEATER_ENABLED

static TIM_HandleTypeDef heater_timer;
static bool heater_ready;
static bool heater_timer_clock_enabled;
static bool heater_active;

static void configure_heater_pin(uint32_t mode, uint32_t alternate)
{
    GPIO_InitTypeDef pin = {
        .Pin = BOARD_IMU_HEATER_PIN,
        .Mode = mode,
        .Pull = GPIO_NOPULL,
        .Speed = GPIO_SPEED_FREQ_VERY_HIGH,
        .Alternate = alternate,
    };
    HAL_GPIO_Init(BOARD_IMU_HEATER_PORT, &pin);
}

void board_imu_heater_force_off(void)
{
    if (heater_timer_clock_enabled) {
        BOARD_IMU_HEATER_TIMER->CCR1 = 0u;
        BOARD_IMU_HEATER_TIMER->CCER &= ~TIM_CCER_CC1E;
        BOARD_IMU_HEATER_TIMER->CR1 &= ~TIM_CR1_CEN;
        configure_heater_pin(GPIO_MODE_OUTPUT_PP, 0u);
        HAL_GPIO_WritePin(BOARD_IMU_HEATER_PORT, BOARD_IMU_HEATER_PIN,
                          GPIO_PIN_RESET);
    }
    heater_active = false;
}

void board_imu_heater_set_duty(uint16_t duty)
{
    if (!heater_ready) {
        board_imu_heater_force_off();
        return;
    }
    const uint16_t bounded_duty = duty <= BOARD_IMU_HEATER_MAX_DUTY
                                      ? duty
                                      : BOARD_IMU_HEATER_MAX_DUTY;
    if (bounded_duty == 0u) {
        if (!heater_active) {
            return;
        }
        board_imu_heater_force_off();
        return;
    }
    if (!heater_active) {
        configure_heater_pin(GPIO_MODE_AF_PP, BOARD_IMU_HEATER_ALTERNATE);
        BOARD_IMU_HEATER_TIMER->CCER |= TIM_CCER_CC1E;
        BOARD_IMU_HEATER_TIMER->CR1 |= TIM_CR1_CEN;
        heater_active = true;
    }
    BOARD_IMU_HEATER_TIMER->CCR1 = bounded_duty;
}

bool board_imu_heater_init(void)
{
    TIM_OC_InitTypeDef channel = {0};

    heater_ready = false;
    heater_active = false;

    __HAL_RCC_GPIOF_CLK_ENABLE();
    RCC->APB2ENR |= RCC_APB2ENR_TIM10EN;
    (void)RCC->APB2ENR;
    heater_timer_clock_enabled = true;
    heater_active = true;
    board_imu_heater_force_off();

    heater_timer.Instance = BOARD_IMU_HEATER_TIMER;
    heater_timer.Init.Prescaler = 0u;
    heater_timer.Init.CounterMode = TIM_COUNTERMODE_UP;
    heater_timer.Init.Period = BOARD_IMU_HEATER_PERIOD;
    heater_timer.Init.ClockDivision = TIM_CLOCKDIVISION_DIV1;
    heater_timer.Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_DISABLE;
    if (HAL_TIM_PWM_Init(&heater_timer) != HAL_OK) {
        board_imu_heater_force_off();
        return false;
    }

    channel.OCMode = TIM_OCMODE_PWM1;
    channel.Pulse = 0u;
    channel.OCPolarity = TIM_OCPOLARITY_HIGH;
    channel.OCFastMode = TIM_OCFAST_DISABLE;
    if (HAL_TIM_PWM_ConfigChannel(&heater_timer, &channel,
                                  BOARD_IMU_HEATER_CHANNEL) != HAL_OK) {
        board_imu_heater_force_off();
        return false;
    }

    configure_heater_pin(GPIO_MODE_AF_PP, BOARD_IMU_HEATER_ALTERNATE);
    if (HAL_TIM_PWM_Start(&heater_timer, BOARD_IMU_HEATER_CHANNEL) != HAL_OK) {
        board_imu_heater_force_off();
        return false;
    }
    heater_active = true;
    board_imu_heater_force_off();
    heater_ready = true;
    return true;
}

#else

void board_imu_heater_force_off(void)
{
}

void board_imu_heater_set_duty(uint16_t duty)
{
    (void)duty;
}

bool board_imu_heater_init(void)
{
    return true;
}

#endif
