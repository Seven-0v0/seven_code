#include <math.h>

#include "imu_drift.h"
#include "imu_stationarity.h"
#include "test_support.h"

static const imu_stationarity_config test_config = {
    .max_gyro_magnitude_dps = 1.0f,
    .min_acceleration_magnitude_g = 0.5f,
    .max_acceleration_magnitude_g = 1.5f,
};

static const imu_acceleration_g test_level_accel = {0.0f, 0.0f, 1.0f};

static void test_fresh_drift_reports_zero_statistics(void) {
    imu_gyro_drift drift;
    imu_gyro_drift_init(&drift, &test_config);

    const imu_drift_statistics stats = imu_gyro_drift_statistics(&drift);

    TEST_CHECK_EQ_INT(stats.stationary_samples, 0u,
                      "fresh drift must report no samples");
    TEST_CHECK_NEAR_FLOAT(stats.stationary_duration_s, 0.0f, 0.0f,
                          "fresh drift must report zero duration");
    TEST_CHECK_NEAR_FLOAT(stats.rms_dps.x_dps, 0.0f, 0.0f,
                          "fresh drift must report zero x noise");
    TEST_CHECK_NEAR_FLOAT(stats.yaw_drift_deg, 0.0f, 0.0f,
                          "fresh drift must report zero yaw drift");
    TEST_CHECK_NEAR_FLOAT(stats.yaw_drift_deg_per_min, 0.0f, 0.0f,
                          "fresh drift must report zero yaw drift rate");
}

static void test_non_stationary_and_non_positive_dt_are_rejected(void) {
    imu_gyro_drift drift;
    imu_gyro_drift_init(&drift, &test_config);

    const imu_gyro_dps moving = {3.0f, 0.0f, 0.0f};
    const imu_acceleration_g free_fall = {0.0f, 0.0f, 0.0f};
    const imu_gyro_dps quiet = {0.0f, 0.0f, 0.0f};

    TEST_CHECK(!imu_gyro_drift_update(&drift, moving, test_level_accel, 0.1f),
               "a moving sample must be rejected");
    TEST_CHECK(!imu_gyro_drift_update(&drift, quiet, free_fall, 0.1f),
               "free fall must be rejected");
    TEST_CHECK(!imu_gyro_drift_update(&drift, quiet, test_level_accel, 0.0f),
               "zero dt must be rejected");
    TEST_CHECK_EQ_INT(drift.stationary_samples, 0u,
                      "rejected samples must not count");
}

static void test_constant_bias_reports_mean_but_zero_noise_rms(void) {
    imu_gyro_drift drift;
    imu_gyro_drift_init(&drift, &test_config);
    const imu_gyro_dps biased = {0.3f, 0.0f, 0.0f};

    for (int sample = 0; sample < 10; sample++) {
        TEST_CHECK(imu_gyro_drift_update(&drift, biased, test_level_accel,
                                         0.1f),
                   "quiet biased sample must be accepted");
    }

    const imu_drift_statistics stats = imu_gyro_drift_statistics(&drift);

    TEST_CHECK_EQ_INT(stats.stationary_samples, 10u,
                      "all ten biased samples must count");
    TEST_CHECK_NEAR_FLOAT(stats.stationary_duration_s, 1.0f, 1e-5f,
                          "ten 0.1 s samples must total one second");
    TEST_CHECK_NEAR_FLOAT(stats.mean_dps.x_dps, 0.3f, 1e-6f,
                          "constant x bias must appear in the mean");
    TEST_CHECK_NEAR_FLOAT(stats.rms_dps.x_dps, 0.0f, 1e-5f,
                          "a constant bias is not noise, so RMS must be zero");
}

static void test_alternating_noise_reports_ac_rms(void) {
    imu_gyro_drift drift;
    imu_gyro_drift_init(&drift, &test_config);
    const imu_gyro_dps noise_positive = {0.0f, 0.0f, 0.5f};
    const imu_gyro_dps noise_negative = {0.0f, 0.0f, -0.5f};

    for (int sample = 0; sample < 5; sample++) {
        imu_gyro_drift_update(&drift, noise_positive, test_level_accel, 0.1f);
        imu_gyro_drift_update(&drift, noise_negative, test_level_accel, 0.1f);
    }

    const imu_drift_statistics stats = imu_gyro_drift_statistics(&drift);

    TEST_CHECK_NEAR_FLOAT(stats.mean_dps.z_dps, 0.0f, 1e-6f,
                          "symmetric noise must average to zero");
    TEST_CHECK_NEAR_FLOAT(stats.rms_dps.z_dps, 0.5f, 1e-4f,
                          "plus/minus 0.5 dps noise must give 0.5 RMS");
}

static void test_yaw_drift_rate_scales_to_degrees_per_minute(void) {
    imu_gyro_drift drift;
    imu_gyro_drift_init(&drift, &test_config);
    const imu_gyro_dps yaw_bias = {0.0f, 0.0f, 0.5f};

    for (int sample = 0; sample < 60; sample++) {
        imu_gyro_drift_update(&drift, yaw_bias, test_level_accel, 0.5f);
    }

    const imu_drift_statistics stats = imu_gyro_drift_statistics(&drift);

    TEST_CHECK_NEAR_FLOAT(stats.stationary_duration_s, 30.0f, 1e-4f,
                          "sixty 0.5 s samples must total thirty seconds");
    TEST_CHECK_NEAR_FLOAT(stats.yaw_drift_deg, 15.0f, 1e-3f,
                          "0.5 dps over 30 s must integrate to 15 degrees");
    TEST_CHECK_NEAR_FLOAT(stats.yaw_drift_deg_per_min, 30.0f, 1e-3f,
                          "0.5 dps must report 30 degrees per minute");
}

static void test_rejected_samples_do_not_advance_duration(void) {
    imu_gyro_drift drift;
    imu_gyro_drift_init(&drift, &test_config);
    const imu_gyro_dps biased = {0.0f, 0.0f, 0.5f};
    const imu_gyro_dps moving = {3.0f, 0.0f, 0.0f};

    imu_gyro_drift_update(&drift, biased, test_level_accel, 0.5f);
    imu_gyro_drift_update(&drift, moving, test_level_accel, 0.5f);
    imu_gyro_drift_update(&drift, moving, test_level_accel, 0.5f);

    const imu_drift_statistics stats = imu_gyro_drift_statistics(&drift);

    TEST_CHECK_EQ_INT(stats.stationary_samples, 1u,
                      "moving samples must not count toward the mean");
    TEST_CHECK_NEAR_FLOAT(stats.stationary_duration_s, 0.5f, 1e-6f,
                          "moving samples must not extend the duration");
}

int main(void) {
    test_fresh_drift_reports_zero_statistics();
    test_non_stationary_and_non_positive_dt_are_rejected();
    test_constant_bias_reports_mean_but_zero_noise_rms();
    test_alternating_noise_reports_ac_rms();
    test_yaw_drift_rate_scales_to_degrees_per_minute();
    test_rejected_samples_do_not_advance_duration();
    return test_support_result();
}
