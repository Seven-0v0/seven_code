#include <stdint.h>

#include "imu_stationary_experiment.h"
#include "test_support.h"

static const imu_acceleration_g level_acceleration = {0.0f, 0.0f, 1.0f};

static imu_stationary_experiment_config test_config(float settling_s,
                                                    float calibration_s,
                                                    float hold_out_s) {
    return (imu_stationary_experiment_config){
        .stationarity = {
            .max_gyro_magnitude_dps = 2.0f,
            .min_acceleration_magnitude_g = 0.5f,
            .max_acceleration_magnitude_g = 1.5f,
        },
        .raw_to_dps_numerator = 125u,
        .raw_to_dps_denominator = 32768u,
        .sample_rate_hz = 100u,
        .settling_duration_s = settling_s,
        .calibration_duration_s = calibration_s,
        .hold_out_duration_s = hold_out_s,
    };
}

static imu_gyro_raw_counts raw_counts(int16_t x, int16_t y, int16_t z) {
    return (imu_gyro_raw_counts){x, y, z};
}

static bool update(imu_stationary_experiment *experiment,
                   imu_gyro_raw_counts gyro_raw,
                   imu_acceleration_g acceleration_g, float dt_s,
                   uint32_t skipped_cycles) {
    const imu_stationary_experiment_input input = {
        .gyro_raw = gyro_raw,
        .acceleration_g = acceleration_g,
        .dt_s = dt_s,
        .skipped_cycles = skipped_cycles,
        .acceleration_valid = true,
    };
    return imu_stationary_experiment_update(experiment, &input);
}

static void test_motion_resets_contiguous_settling_and_calibration(void) {
    imu_stationary_experiment experiment;
    const imu_stationary_experiment_config config = test_config(0.03f, 0.03f,
                                                                 0.03f);
    imu_stationary_experiment_init(&experiment, &config);

    TEST_CHECK(update(&experiment, raw_counts(0, 0, 0), level_acceleration,
                      0.01f, 0u),
               "stationary settling sample must be accepted");
    TEST_CHECK(!update(&experiment, raw_counts(2000, 0, 0),
                       level_acceleration, 0.01f, 0u),
               "motion must be rejected");
    TEST_CHECK_EQ_INT(experiment.settling_accepted_samples, 0u,
                      "motion must reset the settling contiguous window");
    TEST_CHECK_NEAR_FLOAT(experiment.settling_elapsed_s, 0.0f, 0.0f,
                          "motion must reset settling elapsed time");

    for (uint32_t sample = 0u; sample < 3u; sample++) {
        (void)update(&experiment, raw_counts(0, 0, 0), level_acceleration,
                     0.01f, 0u);
    }
    TEST_CHECK(experiment.phase == IMU_EXPERIMENT_CALIBRATING,
               "settling must transition to batch calibration");

    (void)update(&experiment, raw_counts(100, 0, 0), level_acceleration,
                 0.01f, 0u);
    (void)update(&experiment, raw_counts(2000, 0, 0), level_acceleration,
                 0.01f, 0u);
    TEST_CHECK_EQ_INT(experiment.calibration_accepted_samples, 0u,
                      "motion must reset calibration sums and duration");
    for (uint32_t sample = 0u; sample < 3u; sample++) {
        (void)update(&experiment, raw_counts(200, 0, 0), level_acceleration,
                     0.01f, 0u);
    }
    TEST_CHECK(experiment.phase == IMU_EXPERIMENT_HOLD_OUT,
               "calibration must freeze and enter hold-out automatically");
    TEST_CHECK(experiment.bias_frozen,
               "calibration completion must freeze the estimate");
    TEST_CHECK_EQ_INT(experiment.calibration_accepted_samples, 3u,
                      "calibration must count only the contiguous batch");
}

static void test_sixty_thousand_raw_counts_preserve_batch_mean(void) {
    imu_stationary_experiment experiment;
    const imu_stationary_experiment_config config =
        test_config(0.0f, 600.0f, 600.0f);
    imu_stationary_experiment_init(&experiment, &config);

    for (uint32_t sample = 0u; sample < 60000u; sample++) {
        const int16_t x = (sample & 1u) == 0u ? 100 : 101;
        (void)update(&experiment, raw_counts(x, -200, 300), level_acceleration,
                     0.01f, 0u);
    }

    TEST_CHECK(experiment.phase == IMU_EXPERIMENT_HOLD_OUT,
               "60000 samples at 100 Hz must complete calibration");
    TEST_CHECK_EQ_INT(experiment.calibration_accepted_samples, 60000u,
                      "all 60000 stationary samples must be retained");
    TEST_CHECK_EQ_INT(experiment.raw_sum_x, 6030000,
                      "int64 accumulation must retain every half-count pair");
    TEST_CHECK_NEAR_FLOAT(experiment.frozen_bias_dps.x_dps,
                          (double)100.5 * 125.0 / 32768.0, 1e-7f,
                          "raw-count batch mean must retain sub-mdps precision");
    TEST_CHECK_NEAR_FLOAT(experiment.frozen_bias_dps.y_dps,
                          (double)-200.0 * 125.0 / 32768.0, 1e-7f,
                          "negative raw-count mean must remain exact");
}

static void test_hold_out_never_updates_bias_and_reports_honest_durations(void) {
    imu_stationary_experiment experiment;
    const imu_stationary_experiment_config config =
        test_config(0.0f, 0.02f, 0.04f);
    imu_stationary_experiment_init(&experiment, &config);

    for (uint32_t sample = 0u; sample < 2u; sample++) {
        (void)update(&experiment, raw_counts(100, 0, 0), level_acceleration,
                     0.01f, 0u);
    }
    const imu_gyro_dps frozen_bias = experiment.frozen_bias_dps;
    TEST_CHECK(experiment.phase == IMU_EXPERIMENT_HOLD_OUT,
               "calibration must start hold-out immediately");

    (void)update(&experiment, raw_counts(200, 0, 0), level_acceleration,
                 0.01f, 0u);
    TEST_CHECK_NEAR_FLOAT(experiment.frozen_bias_dps.x_dps, frozen_bias.x_dps,
                          0.0f, "hold-out must not update the frozen bias");
    TEST_CHECK_EQ_INT(experiment.hold_out_accepted_samples, 1u,
                      "stationary hold-out sample must be accepted");
    TEST_CHECK_NEAR_FLOAT(experiment.hold_out_wall_duration_s, 0.01f, 1e-6f,
                          "hold-out must report wall-clock duration");

    (void)update(&experiment, raw_counts(200, 0, 0),
                 (imu_acceleration_g){0.0f, 0.0f, 0.0f}, 0.01f, 0u);
    TEST_CHECK_EQ_INT(experiment.hold_out_accepted_samples, 1u,
                      "motion must not count as stationary hold-out evidence");
    TEST_CHECK(!experiment.hold_out_valid,
               "motion must invalidate the hold-out result");
    TEST_CHECK_NEAR_FLOAT(experiment.hold_out_wall_duration_s, 0.02f, 1e-6f,
                          "wall-clock duration must include invalid samples");
}

static void test_skipped_interval_reports_unobserved_time(void) {
    imu_stationary_experiment experiment;
    const imu_stationary_experiment_config config =
        test_config(0.0f, 0.01f, 0.04f);
    imu_stationary_experiment_init(&experiment, &config);
    (void)update(&experiment, raw_counts(1, 0, 0), level_acceleration,
                 0.01f, 0u);

    (void)update(&experiment, raw_counts(1, 0, 0), level_acceleration,
                 0.03f, 2u);

    TEST_CHECK(!experiment.hold_out_valid,
               "skipped conversions must invalidate hold-out");
    TEST_CHECK_NEAR_FLOAT(experiment.hold_out_wall_duration_s, 0.03f, 1e-6f,
                          "wall duration must retain the scheduler interval");
    TEST_CHECK_NEAR_FLOAT(experiment.hold_out_unobserved_duration_s, 0.02f,
                          1e-6f,
                          "only the missing conversions are unobserved");
}

int main(void) {
    test_motion_resets_contiguous_settling_and_calibration();
    test_sixty_thousand_raw_counts_preserve_batch_mean();
    test_hold_out_never_updates_bias_and_reports_honest_durations();
    test_skipped_interval_reports_unobserved_time();
    return test_support_result();
}
