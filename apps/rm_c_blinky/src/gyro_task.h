/* 1 kHz BMI088 IMU observation task for RoboMaster Development Board C.
 *
 * Owns both BMI088 devices after the scheduler starts: it binds the portable
 * drivers to this board's SPI adapter, brings them up, and keeps a plain
 * global snapshot for Ozone/J-Link to read live values.
 *
 * The task polls both BMI088 devices without interrupts or DMA, calibrates
 * gyro bias in RAM, and publishes fixed-rate derived IMU values.
 */
#ifndef APPS_RM_C_BLINKY_GYRO_TASK_H
#define APPS_RM_C_BLINKY_GYRO_TASK_H

#include <stdint.h>

#include "FreeRTOS.h"
#include "task.h"
#include "gyro_snapshot.h"

extern volatile gyro_snapshot g_gyro_snapshot;

/* Creates the IMU task. Call after board_imu_spi_init() but before
 * vTaskStartScheduler(): sensor bring-up runs in the task so reset settling
 * yields to the LED task. Returns pdPASS on success, matching xTaskCreate. */
BaseType_t gyro_task_create(void);

#endif /* APPS_RM_C_BLINKY_GYRO_TASK_H */
