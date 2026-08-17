#ifndef BOARD_RM_DEV_BOARD_C_H
#define BOARD_RM_DEV_BOARD_C_H

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

void board_clock_init(void);
void board_led_init(void);
void board_led_toggle(void);
void board_led_set(uint32_t state);
void board_diagnostic_uart_init(void);
void board_diagnostic_uart_write(const uint8_t *data, uint16_t length);

#endif
