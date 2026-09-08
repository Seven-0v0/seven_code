#include "imu_drift.h"

#include <math.h>

void imu_gyro_drift_init(imu_gyro_drift *drift,
                         const imu_stationarity_config *stationarity) {
    *drift = (imu_gyro_drift){
        .stationarity = *stationarity,
    };
}

bool imu_gyro_drift_update(imu_gyro_drift *drift,
                           imu_gyro_dps gyro_dps,
                           imu_acceleration_g acceleration_g,
                           float dt_s) {
    if (dt_s <= 0.0f ||
        !imu_stationarity_is_stationary(&drift->stationarity, gyro_dps,
                                        acceleration_g)) {
        return false;
    }

    const uint32_t sample_number = drift->stationary_samples + 1u;
    const float weight = 1.0f / (float)sample_number;
    const imu_gyro_dps previous_mean_dps = drift->mean_dps;
    drift->mean_dps.x_dps +=
        (gyro_dps.x_dps - drift->mean_dps.x_dps) * weight;
    drift->mean_dps.y_dps +=
        (gyro_dps.y_dps - drift->mean_dps.y_dps) * weight;
    drift->mean_dps.z_dps +=
        (gyro_dps.z_dps - drift->mean_dps.z_dps) * weight;
    /* Welford variance accumulator: deviation from the old mean times
     * deviation from the new mean is always non-negative, so the squared
     * sum cannot cancel and the RMS noise floor stays exact for a constant
     * signal. */
    drift->sum_squared_deviation_dps2.x_dps +=
        (gyro_dps.x_dps - previous_mean_dps.x_dps) *
        (gyro_dps.x_dps - drift->mean_dps.x_dps);
    drift->sum_squared_deviation_dps2.y_dps +=
        (gyro_dps.y_dps - previous_mean_dps.y_dps) *
        (gyro_dps.y_dps - drift->mean_dps.y_dps);
    drift->sum_squared_deviation_dps2.z_dps +=
        (gyro_dps.z_dps - previous_mean_dps.z_dps) *
        (gyro_dps.z_dps - drift->mean_dps.z_dps);
    drift->stationary_samples = sample_number;
    drift->stationary_duration_s += dt_s;
    drift->yaw_drift_deg += gyro_dps.z_dps * dt_s;
    return true;
}

imu_drift_statistics imu_gyro_drift_statistics(const imu_gyro_drift *drift) {
    if (drift->stationary_samples == 0u) {
        return (imu_drift_statistics){0};
    }

    const float inverse_sample_count =
        1.0f / (float)drift->stationary_samples;
    const imu_gyro_dps rms_dps = {
        sqrtf(drift->sum_squared_deviation_dps2.x_dps * inverse_sample_count),
        sqrtf(drift->sum_squared_deviation_dps2.y_dps * inverse_sample_count),
        sqrtf(drift->sum_squared_deviation_dps2.z_dps * inverse_sample_count),
    };
    float yaw_drift_deg_per_min = 0.0f;
    if (drift->stationary_duration_s > 0.0f) {
        yaw_drift_deg_per_min =
            drift->yaw_drift_deg * 60.0f / drift->stationary_duration_s;
    }
    return (imu_drift_statistics){
        .stationary_samples = drift->stationary_samples,
        .stationary_duration_s = drift->stationary_duration_s,
        .mean_dps = drift->mean_dps,
        .rms_dps = rms_dps,
        .yaw_drift_deg = drift->yaw_drift_deg,
        .yaw_drift_deg_per_min = yaw_drift_deg_per_min,
    };
}
