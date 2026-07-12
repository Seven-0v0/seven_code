#ifndef BOARD_BLUEPILL_F103C8_H
#define BOARD_BLUEPILL_F103C8_H

/* ----------------------------------------------------------------
 * BluePill F103C8 板级定义
 * -------------------------------------------------------------- */

// LED: PA0 / PA1
#define LED_GPIO_PORT GPIOA
#define LED_PIN GPIO_PIN_0

// UART: PA9(TX) / PA10(RX)
#define UART_TX_PIN GPIO_PIN_9
#define UART_RX_PIN GPIO_PIN_10
#define UART_GPIO_PORT GPIOA

// Clock: 72MHz
#define BOARD_CPU_FREQ 72000000UL

#endif /* BOARD_BLUEPILL_F103C8_H */
