#include <stdint.h>

#include "imu_calibration.h"
#include "imu_stationarity.h"
#include "test_support.h"

static const imu_stationarity_config test_config = {
    .max_gyro_magnitude_dps = 1.0f,
    .min_acceleration_magnitude_g = 0.5f,
    .max_acceleration_magnitude_g = 1.5f,
};

static const imu_acceleration_g test_level_accel = {0.0f, 0.0f, 1.0f};

static void test_calibration_accumulates_the_stationary_mean(void) {
    imu_gyro_calibration calibration;
    imu_gyro_calibration_init(&calibration, &test_config, 3u);

    const imu_gyro_dps first = {0.2f, -0.1f, 0.05f};
    TEST_CHECK(imu_gyro_calibration_update(&calibration, first,
                                           test_level_accel),
               "first stationary sample must be accepted");
    TEST_CHECK_EQ_INT(calibration.accepted_samples, 1u,
                      "first sample must count");
    TEST_CHECK_NEAR_FLOAT(calibration.bias_dps.x_dps, 0.2f, 1e-6f,
                          "first sample must set the x bias");

    const imu_gyro_dps second = {0.4f, 0.3f, 0.15f};
    TEST_CHECK(imu_gyro_calibration_update(&calibration, second,
                                           test_level_accel),
               "second stationary sample must be accepted");
    TEST_CHECK_NEAR_FLOAT(calibration.bias_dps.x_dps, 0.3f, 1e-6f,
                          "x bias must be the running mean");

    const imu_gyro_dps third = {0.6f, 0.1f, 0.25f};
    TEST_CHECK(imu_gyro_calibration_update(&calibration, third,
                                           test_level_accel),
               "third stationary sample must be accepted");
    TEST_CHECK_NEAR_FLOAT(calibration.bias_dps.x_dps, 0.4f, 1e-5f,
                          "x bias must average 0.2/0.4/0.6");
    TEST_CHECK_NEAR_FLOAT(calibration.bias_dps.y_dps, 0.1f, 1e-5f,
                          "y bias must average -0.1/0.3/0.1");
    TEST_CHECK_NEAR_FLOAT(calibration.bias_dps.z_dps, 0.15f, 1e-5f,
                          "z bias must average 0.05/0.15/0.25");
    TEST_CHECK(calibration.state == IMU_CALIBRATION_COMPLETE,
               "required sample count must complete calibration");
}

static void test_calibration_rejects_motion_and_free_fall(void) {
    imu_gyro_calibration calibration;
    imu_gyro_calibration_init(&calibration, &test_config, 3u);

    const imu_gyro_dps spinning = {5.0f, 0.0f, 0.0f};
    const imu_acceleration_g free_fall = {0.0f, 0.0f, 0.0f};
    TEST_CHECK(!imu_gyro_calibration_update(&calibration, spinning,
                                            test_level_accel),
               "a moving sample must be rejected");
    TEST_CHECK(!imu_gyro_calibration_update(&calibration, spinning, free_fall),
               "free fall must be rejected");
    TEST_CHECK_EQ_INT(calibration.accepted_samples, 0u,
                      "rejected samples must not count");
    TEST_CHECK(calibration.state == IMU_CALIBRATION_COLLECTING,
               "rejection must keep collecting");
}

static void test_calibration_ignores_updates_after_completion(void) {
    imu_gyro_calibration calibration;
    imu_gyro_calibration_init(&calibration, &test_config, 1u);
    const imu_gyro_dps first = {0.5f, 0.0f, 0.0f};

    TEST_CHECK(imu_gyro_calibration_update(&calibration, first,
                                           test_level_accel),
               "the completing sample must be accepted");
    TEST_CHECK(calibration.state == IMU_CALIBRATION_COMPLETE,
               "one required sample must complete calibration");

    const imu_gyro_dps later = {0.9f, 0.0f, 0.0f};
    TEST_CHECK(!imu_gyro_calibration_update(&calibration, later,
                                            test_level_accel),
               "updates after completion must be refused");
    TEST_CHECK_NEAR_FLOAT(calibration.bias_dps.x_dps, 0.5f, 1e-6f,
                          "completed bias must stay fixed");
}

static void test_zero_required_samples_completes_immediately(void) {
    imu_gyro_calibration calibration;
    imu_gyro_calibration_init(&calibration, &test_config, 0u);
    const imu_gyro_dps sample = {0.5f, 0.0f, 0.0f};

    TEST_CHECK(calibration.state == IMU_CALIBRATION_COMPLETE,
               "zero required samples must complete at init");
    TEST_CHECK(!imu_gyro_calibration_update(&calibration, sample,
                                            test_level_accel),
               "completed calibration must refuse samples");
    TEST_CHECK_NEAR_FLOAT(calibration.bias_dps.x_dps, 0.0f, 0.0f,
                          "bias must remain zero");
}

int main(void) {
    test_calibration_accumulates_the_stationary_mean();
    test_calibration_rejects_motion_and_free_fall();
    test_calibration_ignores_updates_after_completion();
    test_zero_required_samples_completes_immediately();
    return test_support_result();
}
