#include "imu_angular_acceleration.h"
#include "imu_types.h"
#include "test_support.h"

static void test_first_sample_reports_zero_acceleration(void) {
    imu_angular_acceleration_filter filter;
    imu_angular_acceleration_init(&filter, 1.0f);
    const imu_gyro_dps quiet = {0.0f, 0.0f, 0.0f};

    const imu_angular_acceleration_dps2 output =
        imu_angular_acceleration_update(&filter, quiet, 0.01f);

    TEST_CHECK_NEAR_FLOAT(output.x_dps2, 0.0f, 0.0f,
                          "first sample must not produce acceleration");
    TEST_CHECK_NEAR_FLOAT(output.y_dps2, 0.0f, 0.0f,
                          "first sample must not produce y acceleration");
}

static void test_constant_rate_ramp_reports_constant_dps2(void) {
    imu_angular_acceleration_filter filter;
    imu_angular_acceleration_init(&filter, 1.0f);

    const imu_gyro_dps step_0 = {0.0f, 0.0f, 0.0f};
    imu_angular_acceleration_update(&filter, step_0, 0.01f);

    const imu_gyro_dps step_1 = {10.0f, 0.0f, 0.0f};
    const imu_angular_acceleration_dps2 ramp_start =
        imu_angular_acceleration_update(&filter, step_1, 0.01f);
    TEST_CHECK_NEAR_FLOAT(ramp_start.x_dps2, 1000.0f, 1e-3f,
                          "10 dps gained in 10 ms must be 1000 dps2");

    const imu_gyro_dps step_2 = {20.0f, 0.0f, 0.0f};
    const imu_angular_acceleration_dps2 ramp_middle =
        imu_angular_acceleration_update(&filter, step_2, 0.01f);
    TEST_CHECK_NEAR_FLOAT(ramp_middle.x_dps2, 1000.0f, 1e-3f,
                          "steady ramp must keep 1000 dps2");

    const imu_gyro_dps step_3 = {30.0f, 0.0f, 0.0f};
    const imu_angular_acceleration_dps2 ramp_end =
        imu_angular_acceleration_update(&filter, step_3, 0.01f);
    TEST_CHECK_NEAR_FLOAT(ramp_end.x_dps2, 1000.0f, 1e-3f,
                          "steady ramp must not accumulate error");
}

static void test_rate_step_down_reports_negative_dps2(void) {
    imu_angular_acceleration_filter filter;
    imu_angular_acceleration_init(&filter, 1.0f);

    const imu_gyro_dps stopped = {0.0f, 0.0f, 0.0f};
    imu_angular_acceleration_update(&filter, stopped, 0.01f);
    const imu_gyro_dps moving = {30.0f, 0.0f, 0.0f};
    imu_angular_acceleration_update(&filter, moving, 0.01f);

    const imu_angular_acceleration_dps2 braking =
        imu_angular_acceleration_update(&filter, stopped, 0.01f);
    TEST_CHECK_NEAR_FLOAT(braking.x_dps2, -3000.0f, 1e-3f,
                          "losing 30 dps in 10 ms must be -3000 dps2");
}

static void test_smoothing_alpha_halves_each_step(void) {
    imu_angular_acceleration_filter filter;
    imu_angular_acceleration_init(&filter, 0.5f);

    const imu_gyro_dps stopped = {0.0f, 0.0f, 0.0f};
    imu_angular_acceleration_update(&filter, stopped, 0.01f);
    const imu_gyro_dps moving = {100.0f, 0.0f, 0.0f};

    const imu_angular_acceleration_dps2 first =
        imu_angular_acceleration_update(&filter, moving, 0.01f);
    TEST_CHECK_NEAR_FLOAT(first.x_dps2, 5000.0f, 1e-2f,
                          "alpha 0.5 must halve the raw 10000 dps2 step");

    const imu_angular_acceleration_dps2 second =
        imu_angular_acceleration_update(&filter, moving, 0.01f);
    TEST_CHECK_NEAR_FLOAT(second.x_dps2, 2500.0f, 1e-2f,
                          "settling step must halve toward zero again");
}

static void test_non_positive_dt_clears_temporal_continuity(void) {
    imu_angular_acceleration_filter filter;
    imu_angular_acceleration_init(&filter, 1.0f);

    const imu_gyro_dps stopped = {0.0f, 0.0f, 0.0f};
    imu_angular_acceleration_update(&filter, stopped, 0.01f);

    const imu_angular_acceleration_dps2 unchanged =
        imu_angular_acceleration_update(&filter, stopped, 0.0f);
    TEST_CHECK_NEAR_FLOAT(unchanged.x_dps2, 0.0f, 0.0f,
                          "zero dt must not produce acceleration");

    const imu_gyro_dps moving = {100.0f, 0.0f, 0.0f};
    imu_angular_acceleration_update(&filter, moving, 0.01f);
    const imu_angular_acceleration_dps2 after_seed =
        imu_angular_acceleration_update(&filter, moving, 0.01f);
    const imu_angular_acceleration_dps2 with_bad_dt =
        imu_angular_acceleration_update(&filter, moving, -1.0f);
    TEST_CHECK_NEAR_FLOAT(with_bad_dt.x_dps2, after_seed.x_dps2, 0.0f,
                          "negative dt must keep the last filtered value");
    const imu_angular_acceleration_dps2 recovered =
        imu_angular_acceleration_update(
            &filter, (imu_gyro_dps){200.0f, 0.0f, 0.0f}, 0.01f);
    TEST_CHECK_NEAR_FLOAT(recovered.x_dps2, after_seed.x_dps2, 0.0f,
                          "first sample after a timing gap must only reseed");
}

static void test_alpha_out_of_range_is_clamped(void) {
    imu_angular_acceleration_filter filter;
    imu_angular_acceleration_init(&filter, 5.0f);

    const imu_gyro_dps stopped = {0.0f, 0.0f, 0.0f};
    imu_angular_acceleration_update(&filter, stopped, 0.01f);
    const imu_gyro_dps moving = {100.0f, 0.0f, 0.0f};

    const imu_angular_acceleration_dps2 raw =
        imu_angular_acceleration_update(&filter, moving, 0.01f);
    TEST_CHECK_NEAR_FLOAT(raw.x_dps2, 10000.0f, 1e-2f,
                          "alpha above one must behave as one");
}

int main(void) {
    test_first_sample_reports_zero_acceleration();
    test_constant_rate_ramp_reports_constant_dps2();
    test_rate_step_down_reports_negative_dps2();
    test_smoothing_alpha_halves_each_step();
    test_non_positive_dt_clears_temporal_continuity();
    test_alpha_out_of_range_is_clamped();
    return test_support_result();
}
