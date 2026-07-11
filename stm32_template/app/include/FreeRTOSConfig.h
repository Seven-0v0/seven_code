#ifndef FREERTOS_CONFIG_H
#define FREERTOS_CONFIG_H

/* ----------------------------------------------------------------
 * STM32F103C8T6 FreeRTOS 配置
 * Cortex-M3 / 72MHz / 1kHz tick / 6K heap / 4 NVIC priority bits
 * -------------------------------------------------------------- */

#include "stm32f1xx_hal.h"   /* for __NVIC_PRIO_BITS */

/* 基础调度配置 */
#define configUSE_PREEMPTION                1
#define configUSE_PORT_OPTIMISED_TASK_SELECTION  1   /* Cortex-M3 has CLZ */
#define configUSE_16_BIT_TICKS              0
#define configIDLE_SHOULD_YIELD             1
#define configMAX_PRIORITIES                5
#define configMINIMAL_STACK_SIZE            ((uint16_t)128)     /* words */
#define configMAX_TASK_NAME_LEN             (16)
#define configTICK_RATE_HZ                  ((TickType_t)1000)
#define configCPU_CLOCK_HZ                  ((unsigned long)72000000)

/* 内存管理 */
#define configTOTAL_HEAP_SIZE               ((size_t)6144)      /* 6K in 20K RAM */
#define configUSE_MALLOC_FAILED_HOOK        1
#define configCHECK_FOR_STACK_OVERFLOW      2   /* Method 2: stack canary */

/* IPC 功能（关，暂不使用） */
#define configUSE_MUTEXES                   0
#define configUSE_RECURSIVE_MUTEXES         0
#define configUSE_COUNTING_SEMAPHORES       0
#define configQUEUE_REGISTRY_SIZE           0
#define configUSE_TIMERS                    0   /* off to save 512B task stack */
#define configUSE_CO_ROUTINES               0

/* 钩子 */
#define configUSE_TICK_HOOK                 1   /* feeds HAL_IncTick */
#define configUSE_IDLE_HOOK                 0

/* 可选 API 包含开关 */
#define INCLUDE_vTaskDelay                  1   /* vTaskDelay() used by all 3 tasks */
#define INCLUDE_xTaskGetSchedulerState      1

/* 调试 */
#define configUSE_TRACE_FACILITY            0
#define configUSE_STATS_FORMATTING_FUNCTIONS 0

/* ----------------------------------------------------------------
 * 中断优先级配置 — Cortex-M3 / STM32F1 (4 bits, group 4)
 * -------------------------------------------------------------- */
#ifdef __NVIC_PRIO_BITS
    #define configPRIO_BITS                 __NVIC_PRIO_BITS
#else
    #define configPRIO_BITS                 4       /* STM32F1 default */
#endif

#define configLIBRARY_LOWEST_INTERRUPT_PRIORITY      15
#define configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY 5

#define configKERNEL_INTERRUPT_PRIORITY \
    (configLIBRARY_LOWEST_INTERRUPT_PRIORITY << (8 - configPRIO_BITS))
#define configMAX_SYSCALL_INTERRUPT_PRIORITY \
    (configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY << (8 - configPRIO_BITS))

/* ----------------------------------------------------------------
 * Handler 改名 — 覆盖 startup_stm32f103xb.s 的 weak 符号
 * FreeRTOS port.c 定义 vPortSVCHandler / xPortPendSVHandler / xPortSysTickHandler
 * 预处理器将它们重命名为向量表期望的 SVC_Handler / PendSV_Handler / SysTick_Handler
 * -------------------------------------------------------------- */
#define vPortSVCHandler         SVC_Handler
#define xPortPendSVHandler      PendSV_Handler
#define xPortSysTickHandler     SysTick_Handler

#endif /* FREERTOS_CONFIG_H */
