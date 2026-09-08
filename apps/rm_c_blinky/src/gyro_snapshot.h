/* Coherent BMI088 state publication for debugger and firmware readers. */
#ifndef APPS_RM_C_BLINKY_GYRO_SNAPSHOT_H
#define APPS_RM_C_BLINKY_GYRO_SNAPSHOT_H

#include <stdbool.h>
#include <stdint.h>

#include "bmi088_accel.h"
#include "bmi088_gyro.h"

#define GYRO_SNAPSHOT_INIT_PENDING (-1)
#define GYRO_SNAPSHOT_ACCEL_INIT_PENDING (-1)
#define GYRO_SNAPSHOT_PAYLOAD_MAX_SIZE 256u

typedef struct {
    uint32_t sequence;
    int32_t init_status;
    uint32_t read_error_count;
    bmi088_gyro_status last_read_status;
    int32_t x_mdps;
    int32_t y_mdps;
    int32_t z_mdps;

    bmi088_accel_status accel_init_status;
    bmi088_accel_status last_accel_read_status;
    uint32_t accel_read_error_count;
    bmi088_accel_status last_temperature_status;
    uint32_t temperature_read_count;
    uint32_t temperature_read_error_count;
    int32_t temperature_mdeg_c;
    int32_t accel_x_ug;
    int32_t accel_y_ug;
    int32_t accel_z_ug;

    /* Scale-calibrated acceleration in the unchanged BMI088 sensor frame. */
    float accel_x_g;
    float accel_y_g;
    float accel_z_g;
    float calibrated_gyro_bias_x_dps;
    float calibrated_gyro_bias_y_dps;
    float calibrated_gyro_bias_z_dps;
    float angular_velocity_x_dps;
    float angular_velocity_y_dps;
    float angular_velocity_z_dps;
    float angular_acceleration_x_dps2;
    float angular_acceleration_y_dps2;
    float angular_acceleration_z_dps2;
    float quaternion_w;
    float quaternion_x;
    float quaternion_y;
    float quaternion_z;
    float roll_deg;
    float pitch_deg;
    float yaw_deg;
    uint32_t calibration_accepted_samples;
    bool calibration_complete;
    uint32_t stationary_samples;
    float stationary_duration_s;
    float drift_mean_x_dps;
    float drift_mean_y_dps;
    float drift_mean_z_dps;
    float drift_rms_x_dps;
    float drift_rms_y_dps;
    float drift_rms_z_dps;
    float yaw_drift_deg;
    float yaw_drift_deg_per_min;
} gyro_snapshot_payload;

_Static_assert(sizeof(gyro_snapshot_payload) <= GYRO_SNAPSHOT_PAYLOAD_MAX_SIZE,
               "gyro snapshot payload exceeds static publication bound");

typedef struct {
    uint32_t generation;
    gyro_snapshot_payload payload;
} gyro_snapshot;

/* The single writer marks generation odd, writes the payload, then publishes
 * the next even generation. Aligned uint32 stores are atomic on Cortex-M4. */
void gyro_snapshot_publish(volatile gyro_snapshot *snapshot,
                           const gyro_snapshot_payload *payload);

/* Readers accept a copy only when generation is equal before and after the
 * payload read and is even. A false return means retry. */
bool gyro_snapshot_read(const volatile gyro_snapshot *snapshot,
                        gyro_snapshot_payload *payload);

#endif
