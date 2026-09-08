#include <stdint.h>

#include "gyro_snapshot.h"
#include "imu_pipeline_snapshot.h"
#include "test_support.h"

static gyro_snapshot_payload payload(uint32_t sequence) {
    gyro_snapshot_payload value = {
        .sequence = sequence,
        .init_status = 0,
        .read_error_count = 3u,
        .last_read_status = BMI088_GYRO_ERR_BUS,
        .gyro_sample_valid = true,
        .x_mdps = 10,
        .y_mdps = -20,
        .z_mdps = 30,
        .sample_dt_s = 0.08f,
        .skipped_cycles = 3u,
        .temperature_valid = true,
        .temperature_slope_degc_per_s = 0.02f,
        .sample_stationary = true,
        .hold_out_accepted_samples = 1200u,
        .experiment_phase = IMU_EXPERIMENT_HOLD_OUT,
        .experiment_phase_elapsed_s = 12.0f,
        .experiment_phase_required_s = 600.0f,
        .hold_out_wall_duration_s = 12.0f,
        .hold_out_accepted_duration_s = 12.0f,
        .hold_out_unobserved_duration_s = 0.0f,
        .bias_frozen = true,
        .hold_out_valid = true,
        .calibrated_yaw_drift_deg = 0.04f,
        .calibrated_yaw_drift_deg_per_min = 12.0f,
    };
    return value;
}

static void test_pipeline_snapshot_preserves_drift_evidence(void) {
    gyro_snapshot_payload snapshot = {0};
    const imu_pipeline_output output = {
        .sample_dt_s = 0.08f,
        .skipped_cycles = 3u,
        .temperature_degc = 25.0f,
        .temperature_slope_degc_per_s = 0.02f,
        .temperature_valid = true,
        .sample_stationary = true,
        .hold_out_bias_dps = {0.1f, -0.2f, 0.3f},
        .drift = {
            .sample_count = 4u,
            .yaw_drift_deg = 0.09f,
            .yaw_drift_deg_per_min = 18.0f,
        },
        .calibrated_drift = {
            .sample_count = 4u,
            .yaw_drift_deg = 0.03f,
            .yaw_drift_deg_per_min = 6.0f,
        },
    };

    imu_pipeline_snapshot_apply(&snapshot, &output);

    TEST_CHECK_NEAR_FLOAT(snapshot.sample_dt_s, 0.08f, 0.0f,
                          "snapshot must retain actual task dt");
    TEST_CHECK_EQ_INT(snapshot.skipped_cycles, 3u,
                      "snapshot must retain skipped task cycles");
    TEST_CHECK(snapshot.temperature_valid,
               "snapshot must retain temperature validity");
    TEST_CHECK(snapshot.sample_stationary,
               "snapshot must retain sample stationarity");
    TEST_CHECK_NEAR_FLOAT(snapshot.yaw_drift_deg, 0.09f, 0.0f,
                          "snapshot must retain raw yaw drift");
    TEST_CHECK_NEAR_FLOAT(snapshot.calibrated_yaw_drift_deg, 0.03f, 0.0f,
                          "snapshot must retain calibrated yaw drift");
}

int main(void) {
    test_pipeline_snapshot_preserves_drift_evidence();
    /* Given: a stable even generation and a complete payload. */
    volatile gyro_snapshot snapshot = {0};
    gyro_snapshot_payload expected = payload(7u);

    /* When: the single writer publishes it. */
    gyro_snapshot_publish(&snapshot, &expected);

    /* Then: generation is even and a reader accepts the exact payload. */
    TEST_CHECK_EQ_INT(snapshot.generation, 2,
                      "first publication must finish at generation 2");
    gyro_snapshot_payload actual = {0};
    TEST_CHECK(gyro_snapshot_read(&snapshot, &actual),
               "stable even snapshot must be readable");
    TEST_CHECK_EQ_INT(actual.sequence, expected.sequence,
                      "reader must receive the published sequence");
    TEST_CHECK_EQ_INT(actual.init_status, expected.init_status,
                      "reader must receive the published init status");
    TEST_CHECK_EQ_INT(actual.read_error_count, expected.read_error_count,
                      "reader must receive the published error count");
    TEST_CHECK_EQ_INT(actual.y_mdps, expected.y_mdps,
                      "reader must receive the published axes");
    TEST_CHECK_EQ_INT(actual.last_read_status, expected.last_read_status,
                       "reader must receive the published read status");
    TEST_CHECK_NEAR_FLOAT(actual.sample_dt_s, expected.sample_dt_s, 0.0f,
                          "reader must receive measured dt evidence");
    TEST_CHECK_EQ_INT(actual.skipped_cycles, expected.skipped_cycles,
                      "reader must receive skipped-cycle evidence");
    TEST_CHECK(actual.temperature_valid,
               "reader must receive temperature validity evidence");
    TEST_CHECK(actual.sample_stationary,
               "reader must receive sample stationarity evidence");
    TEST_CHECK_EQ_INT(actual.experiment_phase, IMU_EXPERIMENT_HOLD_OUT,
                      "reader must receive experiment phase");
    TEST_CHECK(actual.bias_frozen,
               "reader must receive frozen-bias evidence");
    TEST_CHECK(actual.hold_out_valid,
               "reader must receive hold-out validity evidence");
    TEST_CHECK_EQ_INT(actual.hold_out_accepted_samples, 1200u,
                      "reader must receive accepted hold-out count");
    TEST_CHECK_NEAR_FLOAT(actual.hold_out_wall_duration_s, 12.0f, 0.0f,
                          "reader must receive hold-out wall duration");
    TEST_CHECK_NEAR_FLOAT(actual.calibrated_yaw_drift_deg,
                          expected.calibrated_yaw_drift_deg, 0.0f,
                          "reader must receive calibrated drift evidence");

    /* Given: a read failure followed by a successful sample publication. */
    expected.last_read_status = BMI088_GYRO_OK;
    expected.sequence = 8u;
    expected.x_mdps = 40;
    expected.y_mdps = -50;
    expected.z_mdps = 60;

    /* When: the writer publishes the recovery payload. */
    gyro_snapshot_publish(&snapshot, &expected);

    /* Then: one coherent read sees the recovered axes and OK status together. */
    TEST_CHECK(gyro_snapshot_read(&snapshot, &actual),
               "recovery snapshot must be readable");
    TEST_CHECK_EQ_INT(actual.sequence, 8,
                      "recovery must publish its sequence");
    TEST_CHECK_EQ_INT(actual.last_read_status, BMI088_GYRO_OK,
                      "recovery must clear the last read failure");
    TEST_CHECK_EQ_INT(actual.read_error_count, 3,
                      "recovery must preserve accumulated read errors");
    TEST_CHECK_EQ_INT(actual.x_mdps, 40,
                      "recovery must publish its x axis coherently");
    TEST_CHECK_EQ_INT(actual.y_mdps, -50,
                      "recovery must publish its y axis coherently");
    TEST_CHECK_EQ_INT(actual.z_mdps, 60,
                      "recovery must publish its z axis coherently");

    /* Given: an odd generation representing a writer in progress. */
    snapshot.generation = 3u;

    /* When/Then: readers reject it rather than accepting torn fields. */
    TEST_CHECK(!gyro_snapshot_read(&snapshot, &actual),
               "odd generation must be rejected");

    /* Given: the last even generation before uint32 wrap. */
    snapshot.generation = UINT32_MAX - 1u;
    expected = payload(UINT32_MAX);

    /* When: another payload is published across the wrap. */
    gyro_snapshot_publish(&snapshot, &expected);

    /* Then: zero is a valid stable even generation and data remains readable. */
    TEST_CHECK_EQ_INT(snapshot.generation, 0,
                      "generation wrap must preserve even stability");
    TEST_CHECK(gyro_snapshot_read(&snapshot, &actual),
               "wrapped stable snapshot must be readable");
    TEST_CHECK_EQ_INT(actual.sequence, UINT32_MAX,
                      "wrapped publication must preserve its payload");

    return test_support_result();
}
