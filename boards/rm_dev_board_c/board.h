#ifndef BOARD_RM_DEV_BOARD_C_H
#define BOARD_RM_DEV_BOARD_C_H

#include <stdbool.h>
#include <stdint.h>

#include "stm32f4xx_hal.h"

/* Official RoboMaster Development Board C assignments:
 * PH10: LED_B / TIM5_CH1, PA9: USART1_TX, PB7: USART1_RX.
 * This diagnostic uses only PH10 as a GPIO output and PA9 as a TX-only UART.
 */
#define BOARD_CPU_FREQ_HZ 168000000UL
#define BOARD_LED_PORT GPIOH
#define BOARD_LED_PIN GPIO_PIN_10
#define BOARD_DIAGNOSTIC_UART USART1
#define BOARD_DIAGNOSTIC_UART_PORT GPIOA
#define BOARD_DIAGNOSTIC_UART_TX_PIN GPIO_PIN_9

/* Official BMI088 wiring on Development Board C: SPI1 with two software chip
 * selects. PB3 SCK, PB4 MISO, PA7 MOSI are shared; PB0 selects the gyro and
 * PA4 selects the accelerometer. Both selects are active low and idle high.
 * PA4 is driven high by board_imu_spi_init and has no runtime accessor, so the
 * accelerometer stays deselected for the lifetime of the program.
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

void board_clock_init(void);
void board_led_init(void);
void board_led_toggle(void);
void board_led_set(uint32_t state);
void board_diagnostic_uart_init(void);
void board_diagnostic_uart_write(const uint8_t *data, uint16_t length);
void board_imu_spi_init(void);
void board_imu_gyro_select(void);
void board_imu_gyro_deselect(void);
bool board_imu_spi_transfer(const uint8_t *tx, uint8_t *rx, uint16_t length);

#endif
