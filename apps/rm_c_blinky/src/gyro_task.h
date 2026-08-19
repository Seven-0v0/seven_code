/* 50 Hz BMI088 gyroscope telemetry task for RoboMaster Development Board C.
 *
 * Owns the gyroscope end to end after the scheduler starts: it binds the
 * portable driver to this board's SPI adapter, brings the part up, and emits
 * one fixed-point ASCII line per sample on the diagnostic UART. It also keeps
 * a plain global snapshot so a J-Link debugger can read live values without
 * any UART capture.
 *
 * Deliberately absent: accelerometer access, interrupts, DMA, calibration,
 * fusion, and floating point. The task reads, formats, and publishes.
 */
#ifndef APPS_RM_C_BLINKY_GYRO_TASK_H
#define APPS_RM_C_BLINKY_GYRO_TASK_H

#include <stdint.h>

#include "FreeRTOS.h"
#include "task.h"
#include "gyro_snapshot.h"

extern volatile gyro_snapshot g_gyro_snapshot;

/* Creates the gyroscope task. Call after board_imu_spi_init() and after the
 * diagnostic UART is up, but before vTaskStartScheduler(): device bring-up
 * needs an 80 ms settle that must yield rather than spin, so it runs inside
 * the task. Returns pdPASS on success, matching xTaskCreate. */
BaseType_t gyro_task_create(void);

#endif /* APPS_RM_C_BLINKY_GYRO_TASK_H */
