/* Fixed-rate, sensor-frame IMU processing used by the C-board task. */
#ifndef APPS_RM_C_BLINKY_IMU_PIPELINE_H
#define APPS_RM_C_BLINKY_IMU_PIPELINE_H

#include <stdbool.h>
#include <stdint.h>

#include "imu_angular_acceleration.h"
#include "imu_attitude.h"
#include "imu_calibration.h"
#include "imu_drift.h"

#define IMU_PIPELINE_SAMPLE_PERIOD_S 0.02f
#define IMU_PIPELINE_SAMPLE_RATE_HZ 50u
#define IMU_PIPELINE_CALIBRATION_SAMPLES 100u

typedef struct {
    imu_gyro_dps gyro_bias_dps;
    imu_gyro_dps angular_velocity_dps;
    imu_acceleration_g acceleration_g;
    imu_angular_acceleration_dps2 angular_acceleration_dps2;
    imu_attitude_output attitude;
    imu_drift_statistics drift;
    uint32_t calibration_accepted_samples;
    bool calibration_complete;
} imu_pipeline_output;

typedef struct {
    imu_gyro_calibration calibration;
    imu_angular_acceleration_filter angular_acceleration;
    imu_attitude attitude;
    imu_gyro_drift drift;
} imu_pipeline;

void imu_pipeline_init(imu_pipeline *pipeline);

/* Inputs are already confirmed by both sensor drivers and remain in the
 * unchanged BMI088 sensor frame. Rates are dps and acceleration is g. */
imu_pipeline_output imu_pipeline_update(imu_pipeline *pipeline,
                                        imu_gyro_dps gyro_dps,
                                        imu_acceleration_g acceleration_g);

#endif /* APPS_RM_C_BLINKY_IMU_PIPELINE_H */
