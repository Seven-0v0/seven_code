#include <math.h>

#include "imu_stationarity.h"
#include "test_support.h"

static const imu_stationarity_config test_config = {
    .max_gyro_magnitude_dps = 1.0f,
    .min_acceleration_magnitude_g = 0.5f,
    .max_acceleration_magnitude_g = 1.5f,
};

static void test_level_and_quiet_is_stationary(void) {
    const imu_gyro_dps gyro = {0.0f, 0.0f, 0.0f};
    const imu_acceleration_g level = {0.0f, 0.0f, 1.0f};

    TEST_CHECK(imu_stationarity_is_stationary(&test_config, gyro, level),
               "quiet level sensor must be stationary");
}

static void test_tilted_sensor_at_rest_is_stationary(void) {
    const imu_gyro_dps gyro = {0.0f, 0.0f, 0.0f};
    const imu_acceleration_g tilted = {0.0f, 0.5f, 0.8660254f};

    TEST_CHECK(imu_stationarity_is_stationary(&test_config, gyro, tilted),
               "still sensor must stay stationary when tilted");
}

static void test_gyro_magnitude_gate_accepts_below_and_rejects_above(void) {
    const imu_acceleration_g level = {0.0f, 0.0f, 1.0f};
    const imu_gyro_dps under_limit = {0.6f, 0.6f, 0.5f};
    const imu_gyro_dps over_limit = {0.9f, 0.5f, 0.1f};

    TEST_CHECK(imu_stationarity_is_stationary(&test_config, under_limit,
                                              level),
               "gyro magnitude below the limit must pass");
    TEST_CHECK(!imu_stationarity_is_stationary(&test_config, over_limit,
                                               level),
               "gyro magnitude over the limit must be rejected");
}

static void test_acceleration_gate_is_inclusive_at_boundaries(void) {
    const imu_gyro_dps gyro = {0.0f, 0.0f, 0.0f};
    const imu_acceleration_g at_min = {0.0f, 0.0f, 0.5f};
    const imu_acceleration_g at_max = {0.0f, 0.0f, 1.5f};
    const imu_acceleration_g below_min = {0.0f, 0.0f, 0.49f};
    const imu_acceleration_g above_max = {0.0f, 0.0f, 1.51f};

    TEST_CHECK(imu_stationarity_is_stationary(&test_config, gyro, at_min),
               "acceleration magnitude at the lower limit must pass");
    TEST_CHECK(imu_stationarity_is_stationary(&test_config, gyro, at_max),
               "acceleration magnitude at the upper limit must pass");
    TEST_CHECK(!imu_stationarity_is_stationary(&test_config, gyro, below_min),
               "acceleration magnitude below the gate must be rejected");
    TEST_CHECK(!imu_stationarity_is_stationary(&test_config, gyro, above_max),
               "acceleration magnitude above the gate must be rejected");
}

static void test_zero_acceleration_is_rejected_without_nan(void) {
    const imu_gyro_dps gyro = {0.0f, 0.0f, 0.0f};
    const imu_acceleration_g free_fall = {0.0f, 0.0f, 0.0f};

    TEST_CHECK(!imu_stationarity_is_stationary(&test_config, gyro, free_fall),
               "zero acceleration must not count as stationary");
}

int main(void) {
    test_level_and_quiet_is_stationary();
    test_tilted_sensor_at_rest_is_stationary();
    test_gyro_magnitude_gate_accepts_below_and_rejects_above();
    test_acceleration_gate_is_inclusive_at_boundaries();
    test_zero_acceleration_is_rejected_without_nan();
    return test_support_result();
}
