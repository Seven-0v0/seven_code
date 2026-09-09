#include "imu_pipeline.h"
#include "test_support.h"

static const imu_acceleration_g level_acceleration = {0.0f, 0.0f, 1.0f};

static imu_stationary_experiment_config short_config(void) {
    return (imu_stationary_experiment_config){
        .stationarity = {
            .max_gyro_magnitude_dps = 1.0f,
            .min_acceleration_magnitude_g = 0.95f,
            .max_acceleration_magnitude_g = 1.05f,
        },
        .raw_to_dps_numerator = 2000u,
        .raw_to_dps_denominator = 32768u,
        .sample_rate_hz = 100u,
        .settling_duration_s = 0.02f,
        .calibration_duration_s = 0.02f,
        .hold_out_duration_s = 0.03f,
    };
}

static imu_pipeline_input raw_input(int16_t z_raw, float dt_s) {
    return (imu_pipeline_input){
        .gyro_raw = {.z = z_raw},
        .acceleration_g = level_acceleration,
        .dt_s = dt_s,
        .gyro_raw_valid = true,
        .acceleration_valid = true,
        .acceleration_fresh = true,
        .acceleration_sampled = true,
        .acceleration_age_s = 0.0f,
        .acceleration_sample_dt_s = dt_s,
    };
}

static void enter_hold_out(imu_pipeline *pipeline, int16_t bias_raw) {
    imu_pipeline_input input = raw_input(bias_raw, 0.01f);
    (void)imu_pipeline_update_timed(pipeline, &input);
    (void)imu_pipeline_update_timed(pipeline, &input);
    (void)imu_pipeline_update_timed(pipeline, &input);
    (void)imu_pipeline_update_timed(pipeline, &input);
}

static void test_batch_bias_freezes_before_hold_out(void) {
    imu_pipeline pipeline;
    const imu_stationary_experiment_config config = short_config();
    imu_pipeline_init_with_experiment_config(&pipeline, &config);

    enter_hold_out(&pipeline, 1);
    const imu_pipeline_output output = imu_pipeline_update_timed(
        &pipeline, &(imu_pipeline_input){
                       .gyro_raw = {.z = 2},
                       .acceleration_g = level_acceleration,
                       .dt_s = 0.01f,
                       .gyro_raw_valid = true,
                   });

    TEST_CHECK(output.calibration_complete,
               "the contiguous batch must freeze before hold-out");
    TEST_CHECK(output.experiment_phase == IMU_EXPERIMENT_HOLD_OUT,
               "automatic hold-out must be observable");
    TEST_CHECK_NEAR_FLOAT(output.hold_out_bias_dps.z_dps,
                          1.0f * 2000.0f / 32768.0f, 1e-7f,
                          "frozen bias must retain raw-count precision");
    TEST_CHECK_NEAR_FLOAT(output.calibrated_drift.yaw_drift_deg,
                          1.0f * 2000.0f / 32768.0f * 0.01f, 1e-7f,
                          "hold-out must integrate only after bias freeze");
}

static void test_hold_out_integrates_motion_but_marks_it_invalid(void) {
    imu_pipeline pipeline;
    const imu_stationary_experiment_config config = short_config();
    imu_pipeline_init_with_experiment_config(&pipeline, &config);
    enter_hold_out(&pipeline, 1);

    imu_pipeline_input moving = raw_input(2, 0.01f);
    moving.acceleration_g = (imu_acceleration_g){0.0f, 0.0f, 0.0f};
    const imu_pipeline_output output =
        imu_pipeline_update_timed(&pipeline, &moving);

    TEST_CHECK(!output.hold_out_valid,
               "motion must invalidate stationary hold-out evidence");
    TEST_CHECK_NEAR_FLOAT(output.hold_out_wall_duration_s, 0.01f, 1e-7f,
                          "motion must still advance wall-clock duration");
    TEST_CHECK_NEAR_FLOAT(output.hold_out_accepted_duration_s, 0.0f, 0.0f,
                          "motion must not count as stationary evidence");
    TEST_CHECK_NEAR_FLOAT(output.calibrated_drift.yaw_drift_deg,
                          1.0f * 2000.0f / 32768.0f * 0.01f, 1e-7f,
                          "motion must not be suppressed from yaw integration");
    TEST_CHECK(!output.sample_stationary,
               "motion must never be reported as stationary");
}

static void test_skipped_cycle_invalidates_without_changing_bias(void) {
    imu_pipeline pipeline;
    const imu_stationary_experiment_config config = short_config();
    imu_pipeline_init_with_experiment_config(&pipeline, &config);
    enter_hold_out(&pipeline, 1);
    const imu_gyro_dps frozen_bias = pipeline.experiment.frozen_bias_dps;

    imu_pipeline_input skipped = raw_input(1, 0.02f);
    skipped.skipped_cycles = 1u;
    const imu_pipeline_output output =
        imu_pipeline_update_timed(&pipeline, &skipped);

    TEST_CHECK(!output.hold_out_valid,
               "a missing sensor interval must invalidate the run");
    TEST_CHECK_NEAR_FLOAT(output.gyro_bias_dps.z_dps, frozen_bias.z_dps, 0.0f,
                          "hold-out must never update bias after a skip");
    TEST_CHECK_EQ_INT(output.skipped_cycles, 1u,
                      "the snapshot output must expose skipped cycles");
    TEST_CHECK_NEAR_FLOAT(output.hold_out_unobserved_duration_s, 0.01f, 1e-7f,
                          "the missing conversion interval must be explicit");
    TEST_CHECK_NEAR_FLOAT(output.calibrated_drift.observed_duration_s,
                          0.001f, 1e-7f,
                          "the current observed sample must still integrate");
}

static void test_pipeline_uses_configured_raw_scale(void) {
    imu_pipeline pipeline;
    imu_stationary_experiment_config config = short_config();
    config.raw_to_dps_numerator = 125u;
    config.settling_duration_s = 0.0f;
    config.calibration_duration_s = 0.01f;
    imu_pipeline_init_with_experiment_config(&pipeline, &config);

    imu_pipeline_input input = raw_input(100, 0.01f);
    (void)imu_pipeline_update_timed(&pipeline, &input);
    const imu_pipeline_output output =
        imu_pipeline_update_timed(&pipeline, &input);

    TEST_CHECK_NEAR_FLOAT(output.angular_velocity_dps.z_dps, 0.0f, 1e-7f,
                          "signal and bias must use the configured range");
}

static void test_missing_acceleration_keeps_hold_out_integration(void) {
    imu_pipeline pipeline;
    const imu_stationary_experiment_config config = short_config();
    imu_pipeline_init_with_experiment_config(&pipeline, &config);
    enter_hold_out(&pipeline, 1);

    imu_pipeline_input input = raw_input(2, 0.01f);
    input.acceleration_valid = false;
    const imu_pipeline_output output =
        imu_pipeline_update_timed(&pipeline, &input);

    TEST_CHECK(!output.hold_out_valid,
               "missing acceleration must invalidate stationarity evidence");
    TEST_CHECK(!output.sample_stationary,
               "stale acceleration must not report stationary");
    TEST_CHECK_NEAR_FLOAT(output.calibrated_drift.yaw_drift_deg,
                          1.0f * 2000.0f / 32768.0f * 0.01f, 1e-7f,
                          "valid gyro must integrate without acceleration");
}

static void test_advance_time_returns_current_invalid_evidence(void) {
    imu_pipeline pipeline;
    const imu_stationary_experiment_config config = short_config();
    imu_pipeline_init_with_experiment_config(&pipeline, &config);
    enter_hold_out(&pipeline, 1);

    const imu_pipeline_output output =
        imu_pipeline_advance_time(&pipeline, 0.02f);

    TEST_CHECK(!output.hold_out_valid,
               "missing gyro interval must publish invalid hold-out state");
    TEST_CHECK_NEAR_FLOAT(output.hold_out_wall_duration_s, 0.02f, 1e-7f,
                          "missing gyro interval must publish advanced wall time");
    TEST_CHECK_NEAR_FLOAT(output.hold_out_unobserved_duration_s, 0.02f, 1e-7f,
                          "missing gyro interval must publish unobserved time");
}

static void test_disabled_calibration_discards_pre_enable_samples(void) {
    imu_pipeline pipeline;
    const imu_stationary_experiment_config config = short_config();
    imu_pipeline_init_with_experiment_config(&pipeline, &config);
    imu_pipeline_set_calibration_enabled(&pipeline, false);
    imu_pipeline_input input = raw_input(1, 0.01f);

    for (uint32_t sample = 0u; sample < 10u; sample++) {
        (void)imu_pipeline_update_timed(&pipeline, &input);
    }
    imu_pipeline_set_calibration_enabled(&pipeline, true);
    const imu_pipeline_output output =
        imu_pipeline_update_timed(&pipeline, &input);

    TEST_CHECK_EQ_INT(output.settling_accepted_samples, 1u,
                      "enabling must start a fresh settling window");
    TEST_CHECK_EQ_INT(output.calibration_accepted_samples, 0u,
                      "disabled samples must never enter calibration");
}

static imu_pipeline_input zaru_input(int16_t z_raw) {
    return (imu_pipeline_input){
        .gyro_raw = {.z = z_raw},
        .acceleration_g = level_acceleration,
        .dt_s = 0.001f,
        .gyro_raw_valid = true,
        .acceleration_valid = true,
        .acceleration_fresh = true,
        .acceleration_sampled = true,
        .acceleration_age_s = 0.0f,
        .acceleration_sample_dt_s = 0.001f,
    };
}

static void test_invalid_acceleration_sample_breaks_zaru_window(void) {
    imu_pipeline pipeline;
    imu_stationary_experiment_config config = short_config();
    config.settling_duration_s = 0.0f;
    config.calibration_duration_s = 0.0f;
    config.hold_out_duration_s = 8.0f;
    config.sample_rate_hz = 1000u;
    imu_pipeline_init_with_experiment_config(&pipeline, &config);

    const imu_pipeline_input valid = zaru_input(10);
    (void)imu_pipeline_update_timed(&pipeline, &valid);
    TEST_CHECK_EQ_INT(pipeline.zaru.window_sample_count, 1u,
                      "fresh stationary sample must enter the ZARU window");

    imu_pipeline_input invalid = valid;
    invalid.acceleration_valid = false;
    invalid.acceleration_fresh = false;
    invalid.acceleration_sampled = true;
    const imu_pipeline_output output =
        imu_pipeline_update_timed(&pipeline, &invalid);

    TEST_CHECK_EQ_INT(pipeline.zaru.window_sample_count, 0u,
                      "an invalid acceleration sample must clear ZARU evidence");
    TEST_CHECK(!output.sample_stationary,
               "an invalid acceleration sample is not confirmed stationary");
}

static void test_confirmed_static_holdout_locks_quaternion_heading(void) {
    imu_pipeline pipeline;
    imu_stationary_experiment_config config = short_config();
    config.settling_duration_s = 0.0f;
    config.calibration_duration_s = 0.01f;
    config.hold_out_duration_s = 1.0f;
    config.sample_rate_hz = 1000u;
    imu_pipeline_init_with_experiment_config(&pipeline, &config);

    imu_pipeline_input input = zaru_input(10);
    for (uint32_t sample = 0u; sample < 10u; sample++) {
        (void)imu_pipeline_update_timed(&pipeline, &input);
    }
    for (uint32_t sample = 0u; sample < 500u; sample++) {
        (void)imu_pipeline_update_timed(&pipeline, &input);
    }
    const imu_pipeline_output output =
        imu_pipeline_update_timed(&pipeline, &input);

    TEST_CHECK_NEAR_FLOAT(output.attitude_heading_drift_deg, 0.0f, 1e-4f,
                          "confirmed static hold-out must lock quaternion heading");
}

static void test_brief_static_noise_does_not_resume_heading_integration(void) {
    imu_pipeline pipeline;
    imu_stationary_experiment_config config = short_config();
    config.settling_duration_s = 0.0f;
    config.calibration_duration_s = 0.01f;
    config.hold_out_duration_s = 1.0f;
    config.sample_rate_hz = 1000u;
    imu_pipeline_init_with_experiment_config(&pipeline, &config);

    imu_pipeline_input input = zaru_input(0);
    for (uint32_t sample = 0u; sample < 10u; sample++) {
        (void)imu_pipeline_update_timed(&pipeline, &input);
    }
    input = zaru_input(1);
    for (uint32_t sample = 0u; sample < 100u; sample++) {
        (void)imu_pipeline_update_timed(&pipeline, &input);
    }
    TEST_CHECK(pipeline.experiment.hold_out_valid,
               "brief static noise must not invalidate the hold-out");
    TEST_CHECK_NEAR_FLOAT(pipeline.heading.drift_deg, 0.0f, 0.01f,
                          "brief static noise must not integrate heading");
}

static void test_zaru_reduces_quaternion_heading_drift_without_changing_baseline(void) {
    imu_pipeline observer_pipeline;
    imu_pipeline disabled_pipeline;
    imu_stationary_experiment_config config = short_config();
    config.settling_duration_s = 0.0f;
    config.calibration_duration_s = 0.01f;
    config.hold_out_duration_s = 8.0f;
    config.sample_rate_hz = 1000u;
    config.stationarity.max_gyro_magnitude_dps = 1.0f;
    imu_pipeline_init_with_experiment_config(&observer_pipeline, &config);
    imu_pipeline_init_with_experiment_config(&disabled_pipeline, &config);
    imu_pipeline_set_zaru_enabled(&disabled_pipeline, false);

    for (uint32_t sample = 0u; sample < 10u; sample++) {
        const imu_pipeline_input calibration_input = zaru_input(0);
        (void)imu_pipeline_update_timed(&observer_pipeline,
                                        &calibration_input);
        (void)imu_pipeline_update_timed(&disabled_pipeline,
                                        &calibration_input);
    }
    for (uint32_t sample = 0u; sample < 8000u; sample++) {
        const imu_pipeline_input input = zaru_input(10);
        (void)imu_pipeline_update_timed(&observer_pipeline, &input);
        (void)imu_pipeline_update_timed(&disabled_pipeline, &input);
    }
    const imu_pipeline_input final_input = zaru_input(10);
    const imu_pipeline_output observer_output =
        imu_pipeline_update_timed(&observer_pipeline, &final_input);
    const imu_pipeline_output disabled_output =
        imu_pipeline_update_timed(&disabled_pipeline, &final_input);

    TEST_CHECK(fabsf(observer_output.attitude_heading_drift_deg) <
                   fabsf(disabled_output.attitude_heading_drift_deg) * 0.6f,
               "enabled ZARU must reduce actual quaternion heading drift");
    TEST_CHECK(fabsf(disabled_output.attitude_heading_drift_deg) > 0.4f,
               "disabled observer must retain measurable gyro heading drift");
    TEST_CHECK_NEAR_FLOAT(observer_output.calibrated_drift.yaw_drift_deg,
                          disabled_output.calibrated_drift.yaw_drift_deg, 1e-6f,
                          "open-loop calibrated gyro baseline must remain distinct");
}

int main(void) {
    test_batch_bias_freezes_before_hold_out();
    test_hold_out_integrates_motion_but_marks_it_invalid();
    test_skipped_cycle_invalidates_without_changing_bias();
    test_pipeline_uses_configured_raw_scale();
    test_missing_acceleration_keeps_hold_out_integration();
    test_advance_time_returns_current_invalid_evidence();
    test_disabled_calibration_discards_pre_enable_samples();
    test_zaru_reduces_quaternion_heading_drift_without_changing_baseline();
    test_invalid_acceleration_sample_breaks_zaru_window();
    test_confirmed_static_holdout_locks_quaternion_heading();
    test_brief_static_noise_does_not_resume_heading_integration();
    return test_support_result();
}
