#include "imu_pipeline.h"
#include "test_support.h"

int main(void) {
    /* Given: a fresh pipeline and 100 stationary samples at 50 Hz. */
    imu_pipeline pipeline;
    imu_pipeline_init(&pipeline);
    const imu_gyro_dps gyro_dps = {0.5f, -0.25f, 0.125f};
    const imu_acceleration_g acceleration_g = {0.0f, 0.0f, 1.0f};
    imu_pipeline_output output = {0};

    /* Given: an invalid free-fall acceleration sample. */
    /* When: the sample is processed before calibration has started. */
    output = imu_pipeline_update(&pipeline, gyro_dps,
                                  (imu_acceleration_g){0.0f, 0.0f, 0.0f});
    /* Then: stationarity-gated calibration and drift reject the sample. */
    TEST_CHECK_EQ_INT(output.calibration_accepted_samples, 0,
                      "free-fall must not enter gyro calibration");
    TEST_CHECK_EQ_INT(output.drift.stationary_samples, 0,
                      "free-fall must not enter drift statistics");

    /* When: valid sensor samples are processed at the documented fixed dt. */
    for (uint32_t sample = 0u; sample < 100u; sample++) {
        output = imu_pipeline_update(&pipeline, gyro_dps, acceleration_g);
    }

    /* Then: RAM calibration completes and calibrated rate is zero. */
    TEST_CHECK(output.calibration_complete,
               "calibration must complete after required stationary samples");
    TEST_CHECK_NEAR_FLOAT(output.gyro_bias_dps.x_dps, 0.5f, 0.0001f,
                          "x gyro bias must be the stationary mean");
    TEST_CHECK_NEAR_FLOAT(output.angular_velocity_dps.z_dps, 0.0f, 0.0001f,
                          "calibrated z angular velocity must remove bias");
    TEST_CHECK_NEAR_FLOAT(output.acceleration_g.z_g, 1.0f, 0.0001f,
                          "acceleration must retain explicit g units");
    TEST_CHECK_EQ_INT(output.drift.stationary_samples, 100,
                      "drift must consume every valid stationary sample");
    TEST_CHECK_NEAR_FLOAT(output.drift.yaw_drift_deg, 0.25f, 0.0001f,
                          "yaw drift must use the raw stationary z rate");
    TEST_CHECK_NEAR_FLOAT(output.angular_acceleration_dps2.x_dps2, 0.0f,
                          0.0001f, "constant calibrated rate has no angular acceleration");

    return test_support_result();
}
