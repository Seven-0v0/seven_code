#ifndef APPS_RM_C_BLINKY_IMU_PIPELINE_SNAPSHOT_H
#define APPS_RM_C_BLINKY_IMU_PIPELINE_SNAPSHOT_H

#include "gyro_snapshot.h"
#include "imu_pipeline.h"

void imu_pipeline_snapshot_apply(gyro_snapshot_payload *snapshot,
                                 const imu_pipeline_output *output);

#endif /* APPS_RM_C_BLINKY_IMU_PIPELINE_SNAPSHOT_H */
