#include <math.h>

#include "imu_attitude.h"
#include "test_support.h"

static const imu_attitude_config test_config = {
    .proportional_gain = 2.0f,
    .integral_gain = 0.0f,
    .min_acceleration_magnitude_g = 0.5f,
    .max_acceleration_magnitude_g = 1.5f,
};

static const imu_gyro_dps test_zero_gyro = {0.0f, 0.0f, 0.0f};

static void test_level_acceleration_keeps_attitude_level(void) {
    imu_attitude attitude;
    imu_attitude_init(&attitude, &test_config);
    const imu_acceleration_g level = {0.0f, 0.0f, 1.0f};

    imu_attitude_output output;
    for (int step = 0; step < 100; step++) {
        output = imu_attitude_update(&attitude, test_zero_gyro, level, 0.01f);
    }

    TEST_CHECK_NEAR_FLOAT(output.euler_zyx_deg.roll_deg, 0.0f, 1e-4f,
                          "level sensor must keep zero roll");
    TEST_CHECK_NEAR_FLOAT(output.euler_zyx_deg.pitch_deg, 0.0f, 1e-4f,
                          "level sensor must keep zero pitch");
    TEST_CHECK_NEAR_FLOAT(output.euler_zyx_deg.yaw_deg, 0.0f, 1e-4f,
                          "level sensor must keep zero yaw");
    TEST_CHECK_NEAR_FLOAT(output.quaternion.w, 1.0f, 1e-6f,
                          "level sensor must keep the identity quaternion");
}

static void test_positive_roll_tilt_converges_to_thirty_degrees(void) {
    imu_attitude attitude;
    imu_attitude_init(&attitude, &test_config);
    const imu_acceleration_g tilted_right = {0.0f, 0.5f, 0.8660254f};

    imu_attitude_output output;
    for (int step = 0; step < 400; step++) {
        output = imu_attitude_update(&attitude, test_zero_gyro,
                                     tilted_right, 0.01f);
    }

    TEST_CHECK_NEAR_FLOAT(output.euler_zyx_deg.roll_deg, 30.0f, 0.2f,
                          "roll to the right must converge to +30 degrees");
    TEST_CHECK_NEAR_FLOAT(output.euler_zyx_deg.pitch_deg, 0.0f, 0.2f,
                          "pure roll must not report pitch");
}

static void test_negative_roll_tilt_converges_to_minus_thirty(void) {
    imu_attitude attitude;
    imu_attitude_init(&attitude, &test_config);
    const imu_acceleration_g tilted_left = {0.0f, -0.5f, 0.8660254f};

    imu_attitude_output output;
    for (int step = 0; step < 400; step++) {
        output = imu_attitude_update(&attitude, test_zero_gyro, tilted_left,
                                     0.01f);
    }

    TEST_CHECK_NEAR_FLOAT(output.euler_zyx_deg.roll_deg, -30.0f, 0.2f,
                          "roll to the left must converge to -30 degrees");
}

static void test_pitch_tilt_converges_to_thirty_degrees(void) {
    imu_attitude attitude;
    imu_attitude_init(&attitude, &test_config);
    const imu_acceleration_g nose_down = {-0.5f, 0.0f, 0.8660254f};

    imu_attitude_output output;
    for (int step = 0; step < 400; step++) {
        output = imu_attitude_update(&attitude, test_zero_gyro, nose_down,
                                     0.01f);
    }

    TEST_CHECK_NEAR_FLOAT(output.euler_zyx_deg.pitch_deg, 30.0f, 0.2f,
                          "nose-down tilt must converge to +30 degrees");
    TEST_CHECK_NEAR_FLOAT(output.euler_zyx_deg.roll_deg, 0.0f, 0.2f,
                          "pure pitch must not report roll");
}

static void test_tilt_at_two_g_is_rejected_by_the_magnitude_gate(void) {
    imu_attitude attitude;
    imu_attitude_init(&attitude, &test_config);
    const imu_acceleration_g dynamic_two_g = {0.0f, 1.4142135f, 1.4142135f};

    imu_attitude_output output;
    for (int step = 0; step < 200; step++) {
        output = imu_attitude_update(&attitude, test_zero_gyro, dynamic_two_g,
                                     0.01f);
    }

    TEST_CHECK_NEAR_FLOAT(output.euler_zyx_deg.roll_deg, 0.0f, 1e-3f,
                          "2 g acceleration must not tilt the estimate");
    TEST_CHECK_NEAR_FLOAT(output.euler_zyx_deg.pitch_deg, 0.0f, 1e-3f,
                          "2 g acceleration must not change pitch");
}

static void test_same_direction_at_one_g_converges_to_forty_five(void) {
    imu_attitude attitude;
    imu_attitude_init(&attitude, &test_config);
    const imu_acceleration_g tilt_forty_five = {0.0f, 0.7071068f,
                                                0.7071068f};

    imu_attitude_output output;
    for (int step = 0; step < 400; step++) {
        output = imu_attitude_update(&attitude, test_zero_gyro,
                                     tilt_forty_five, 0.01f);
    }

    TEST_CHECK_NEAR_FLOAT(output.euler_zyx_deg.roll_deg, 45.0f, 0.2f,
                          "1 g at 45 degrees must converge to +45 degrees");
}

static void test_zero_acceleration_is_safe_and_gyro_only(void) {
    imu_attitude attitude;
    imu_attitude_init(&attitude, &test_config);
    const imu_acceleration_g free_fall = {0.0f, 0.0f, 0.0f};
    const imu_gyro_dps yaw_rate = {0.0f, 0.0f, 90.0f};

    imu_attitude_output output;
    for (int step = 0; step < 100; step++) {
        output = imu_attitude_update(&attitude, yaw_rate, free_fall, 0.01f);
    }

    TEST_CHECK(isfinite(output.quaternion.w) && isfinite(output.quaternion.x) &&
                   isfinite(output.quaternion.y) && isfinite(output.quaternion.z),
               "zero acceleration must never produce NaN quaternion parts");
    TEST_CHECK(isfinite(output.euler_zyx_deg.roll_deg) &&
                   isfinite(output.euler_zyx_deg.pitch_deg) &&
                   isfinite(output.euler_zyx_deg.yaw_deg),
               "zero acceleration must never produce NaN euler angles");
    const float norm_squared = output.quaternion.w * output.quaternion.w +
                               output.quaternion.x * output.quaternion.x +
                               output.quaternion.y * output.quaternion.y +
                               output.quaternion.z * output.quaternion.z;
    TEST_CHECK_NEAR_FLOAT(norm_squared, 1.0f, 1e-5f,
                          "gyro-only integration must stay normalized");
}

static void test_yaw_integrates_the_gyro_z_rate(void) {
    imu_attitude attitude;
    imu_attitude_init(&attitude, &test_config);
    const imu_acceleration_g level = {0.0f, 0.0f, 1.0f};
    const imu_gyro_dps yaw_rate = {0.0f, 0.0f, 90.0f};

    imu_attitude_output output;
    for (int step = 0; step < 200; step++) {
        output = imu_attitude_update(&attitude, yaw_rate, level, 0.005f);
    }

    TEST_CHECK_NEAR_FLOAT(output.euler_zyx_deg.yaw_deg, 90.0f, 0.1f,
                          "90 dps for one second must integrate to 90 yaw");
    TEST_CHECK_NEAR_FLOAT(output.euler_zyx_deg.roll_deg, 0.0f, 1e-3f,
                          "level yaw rotation must keep zero roll");
    TEST_CHECK_NEAR_FLOAT(output.euler_zyx_deg.pitch_deg, 0.0f, 1e-3f,
                          "level yaw rotation must keep zero pitch");
}

static void test_non_positive_dt_leaves_attitude_unchanged(void) {
    imu_attitude attitude;
    imu_attitude_init(&attitude, &test_config);
    const imu_acceleration_g tilted_right = {0.0f, 0.5f, 0.8660254f};

    const imu_attitude_output frozen =
        imu_attitude_update(&attitude, test_zero_gyro, tilted_right, 0.0f);
    const imu_attitude_output again =
        imu_attitude_update(&attitude, test_zero_gyro, tilted_right, -0.01f);

    TEST_CHECK_NEAR_FLOAT(frozen.euler_zyx_deg.yaw_deg, 0.0f, 1e-6f,
                          "zero dt must not integrate");
    TEST_CHECK_NEAR_FLOAT(again.euler_zyx_deg.yaw_deg, 0.0f, 1e-6f,
                          "negative dt must not integrate");
}

int main(void) {
    test_level_acceleration_keeps_attitude_level();
    test_positive_roll_tilt_converges_to_thirty_degrees();
    test_negative_roll_tilt_converges_to_minus_thirty();
    test_pitch_tilt_converges_to_thirty_degrees();
    test_tilt_at_two_g_is_rejected_by_the_magnitude_gate();
    test_same_direction_at_one_g_converges_to_forty_five();
    test_zero_acceleration_is_safe_and_gyro_only();
    test_yaw_integrates_the_gyro_z_rate();
    test_non_positive_dt_leaves_attitude_unchanged();
    return test_support_result();
}
