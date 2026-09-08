#include "imu_pipeline.h"

static const imu_stationarity_config imu_stationarity = {
    .max_gyro_magnitude_dps = 5.0f,
    .min_acceleration_magnitude_g = 0.8f,
    .max_acceleration_magnitude_g = 1.2f,
};

static imu_gyro_dps subtract_gyro_bias(imu_gyro_dps gyro_dps,
                                       imu_gyro_dps bias_dps) {
    return (imu_gyro_dps){
        .x_dps = gyro_dps.x_dps - bias_dps.x_dps,
        .y_dps = gyro_dps.y_dps - bias_dps.y_dps,
        .z_dps = gyro_dps.z_dps - bias_dps.z_dps,
    };
}

void imu_pipeline_init(imu_pipeline *pipeline) {
    const imu_attitude_config attitude_config = {
        .proportional_gain = 2.0f,
        .integral_gain = 0.1f,
        .min_acceleration_magnitude_g = 0.8f,
        .max_acceleration_magnitude_g = 1.2f,
    };

    imu_gyro_calibration_init(&pipeline->calibration, &imu_stationarity,
                              IMU_PIPELINE_CALIBRATION_SAMPLES);
    imu_angular_acceleration_init(&pipeline->angular_acceleration, 0.25f);
    imu_attitude_init(&pipeline->attitude, &attitude_config);
    imu_gyro_drift_init(&pipeline->drift, &imu_stationarity);
}

imu_pipeline_output imu_pipeline_update(imu_pipeline *pipeline,
                                        imu_gyro_dps gyro_dps,
                                        imu_acceleration_g acceleration_g) {
    (void)imu_gyro_calibration_update(&pipeline->calibration, gyro_dps,
                                      acceleration_g);
    const imu_gyro_dps calibrated_gyro_dps = subtract_gyro_bias(
        gyro_dps, pipeline->calibration.bias_dps);
    const imu_angular_acceleration_dps2 angular_acceleration_dps2 =
        imu_angular_acceleration_update(&pipeline->angular_acceleration,
                                        calibrated_gyro_dps,
                                        IMU_PIPELINE_SAMPLE_PERIOD_S);
    const imu_attitude_output attitude = imu_attitude_update(
        &pipeline->attitude, calibrated_gyro_dps, acceleration_g,
        IMU_PIPELINE_SAMPLE_PERIOD_S);
    (void)imu_gyro_drift_update(&pipeline->drift, gyro_dps, acceleration_g,
                                IMU_PIPELINE_SAMPLE_PERIOD_S);

    return (imu_pipeline_output){
        .gyro_bias_dps = pipeline->calibration.bias_dps,
        .angular_velocity_dps = calibrated_gyro_dps,
        .acceleration_g = acceleration_g,
        .angular_acceleration_dps2 = angular_acceleration_dps2,
        .attitude = attitude,
        .drift = imu_gyro_drift_statistics(&pipeline->drift),
        .calibration_accepted_samples =
            pipeline->calibration.accepted_samples,
        .calibration_complete =
            pipeline->calibration.state == IMU_CALIBRATION_COMPLETE,
    };
}
