#ifndef BOARD_RM_DEV_BOARD_C_H
#define BOARD_RM_DEV_BOARD_C_H

#include <stdbool.h>
#include <stdint.h>

#include "stm32f4xx_hal.h" /* IWYU pragma: keep: public GPIO pin mappings */

/* Official RoboMaster Development Board C assignment: PH10: LED_B / TIM5_CH1. */
#define BOARD_CPU_FREQ_HZ 168000000UL
#define BOARD_LED_PORT GPIOH
#define BOARD_LED_PIN GPIO_PIN_10

/* Official BMI088 wiring on Development Board C: SPI1 with two software chip
 * selects. PB3 SCK, PB4 MISO, PA7 MOSI are shared; PB0 selects the gyro and
 * PA4 selects the accelerometer. Both selects are active low and idle high.
 */
#define BOARD_IMU_SPI SPI1
#define BOARD_IMU_SPI_SCK_PORT GPIOB
#define BOARD_IMU_SPI_SCK_PIN GPIO_PIN_3
#define BOARD_IMU_SPI_MISO_PORT GPIOB
#define BOARD_IMU_SPI_MISO_PIN GPIO_PIN_4
#define BOARD_IMU_SPI_MOSI_PORT GPIOA
#define BOARD_IMU_SPI_MOSI_PIN GPIO_PIN_7
#define BOARD_IMU_GYRO_CS_PORT GPIOB
#define BOARD_IMU_GYRO_CS_PIN GPIO_PIN_0
#define BOARD_IMU_ACCEL_CS_PORT GPIOA
#define BOARD_IMU_ACCEL_CS_PIN GPIO_PIN_4

/* Official example 16 heater output: PF6 / TIM10_CH1 / AF3. TIM10 runs from
 * APB2's 168 MHz timer clock at PSC=0 and ARR=4999 (33.6 kHz PWM). */
#define BOARD_IMU_HEATER_PORT GPIOF
#define BOARD_IMU_HEATER_PIN GPIO_PIN_6
#define BOARD_IMU_HEATER_TIMER TIM10
#define BOARD_IMU_HEATER_CHANNEL TIM_CHANNEL_1
#define BOARD_IMU_HEATER_ALTERNATE GPIO_AF3_TIM10
#define BOARD_IMU_HEATER_PERIOD 4999u
#define BOARD_IMU_HEATER_MAX_DUTY 4500u

void board_clock_init(void);
void board_led_init(void);
void board_led_toggle(void);
void board_led_set(uint32_t state);
void board_imu_spi_init(void);
void board_imu_gyro_select(void);
void board_imu_gyro_deselect(void);
void board_imu_accel_select(void);
void board_imu_accel_deselect(void);
bool board_imu_spi_transfer(const uint8_t *tx, uint8_t *rx, uint16_t length);
bool board_imu_heater_init(void);
void board_imu_heater_set_duty(uint16_t duty);
void board_imu_heater_force_off(void);

#endif
