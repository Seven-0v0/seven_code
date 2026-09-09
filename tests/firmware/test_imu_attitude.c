#include <math.h>
#include <stdbool.h>

#include "imu_attitude.h"
#include "test_support.h"

static const imu_attitude_config test_config = {
    .proportional_gain = 2.0f,
    .integral_gain = 0.0f,
    .min_acceleration_magnitude_g = 0.5f,
    .max_acceleration_magnitude_g = 1.5f,
    .max_acceleration_correction_gyro_dps = 0.0f,
    .max_acceleration_age_s = 0.0f,
};

static const imu_attitude_config gated_test_config = {
    .proportional_gain = 2.0f,
    .integral_gain = 0.0f,
    .min_acceleration_magnitude_g = 0.5f,
    .max_acceleration_magnitude_g = 1.5f,
    .max_acceleration_correction_gyro_dps = 100.0f,
    .max_acceleration_age_s = 0.002f,
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

static void test_quaternion_multiply_conjugate_returns_identity(void) {
    const imu_quaternionf rotation = {
        .w = 0.9238795f,
        .x = 0.0f,
        .y = 0.3826834f,
        .z = 0.0f,
    };
    const imu_quaternionf identity = imu_quaternion_multiply(
        rotation, imu_quaternion_conjugate(rotation));

    TEST_CHECK_NEAR_FLOAT(identity.w, 1.0f, 1e-5f,
                          "q multiplied by conjugate must have unit scalar");
    TEST_CHECK_NEAR_FLOAT(identity.x, 0.0f, 1e-5f,
                          "q multiplied by conjugate must have zero x");
    TEST_CHECK_NEAR_FLOAT(identity.y, 0.0f, 1e-5f,
                          "q multiplied by conjugate must have zero y");
    TEST_CHECK_NEAR_FLOAT(identity.z, 0.0f, 1e-5f,
                          "q multiplied by conjugate must have zero z");
}

static void test_constant_rate_rotation_uses_exact_exponential_map(void) {
    imu_attitude attitude;
    imu_attitude_init(&attitude, &test_config);
    const imu_acceleration_g free_fall = {0.0f, 0.0f, 0.0f};
    const imu_gyro_dps yaw_rate = {0.0f, 0.0f, 90.0f};

    imu_attitude_output output = {0};
    for (int step = 0; step < 1000; step++) {
        output = imu_attitude_update(&attitude, yaw_rate, free_fall, 0.001f);
    }

    TEST_CHECK_NEAR_FLOAT(output.euler_zyx_deg.yaw_deg, 90.0f, 0.001f,
                          "constant-rate rotation must integrate exactly");
    TEST_CHECK_NEAR_FLOAT(output.quaternion.w, 0.7071068f, 1e-5f,
                          "exact yaw rotation must have expected scalar");
    TEST_CHECK_NEAR_FLOAT(output.quaternion.z, 0.7071068f, 1e-5f,
                          "exact yaw rotation must have expected z part");
}

static void test_tiny_angle_rotation_stays_finite_and_normalized(void) {
    imu_attitude attitude;
    imu_attitude_init(&attitude, &test_config);
    const imu_acceleration_g free_fall = {0.0f, 0.0f, 0.0f};
    const imu_gyro_dps tiny_rate = {1.0e-5f, -2.0e-5f, 3.0e-5f};

    imu_attitude_output output = {0};
    for (int step = 0; step < 1000; step++) {
        output = imu_attitude_update(&attitude, tiny_rate, free_fall, 0.001f);
    }

    const float norm_squared = output.quaternion.w * output.quaternion.w +
                               output.quaternion.x * output.quaternion.x +
                               output.quaternion.y * output.quaternion.y +
                               output.quaternion.z * output.quaternion.z;
    TEST_CHECK(isfinite(norm_squared), "tiny-angle output must be finite");
    TEST_CHECK_NEAR_FLOAT(norm_squared, 1.0f, 1e-6f,
                          "tiny-angle output must remain normalized");
}

static void test_two_sample_coning_compensation_matches_rotation_vector(void) {
    imu_attitude attitude;
    imu_attitude_init(&attitude, &test_config);
    const imu_attitude_acceleration invalid = {
        .acceleration_g = {0.0f, 0.0f, 0.0f},
        .valid = false,
        .fresh = false,
        .age_s = 0.0f,
    };
    const float radians_to_degrees = 180.0f / 3.14159265358979323846f;
    const imu_gyro_dps first_rate = {10.0f * radians_to_degrees, 0.0f, 0.0f};
    const imu_gyro_dps second_rate = {0.0f, 20.0f * radians_to_degrees, 0.0f};

    (void)imu_attitude_update_timed(&attitude, first_rate, invalid, 0.01f);
    (void)imu_attitude_update_timed(&attitude, second_rate, invalid, 0.01f);

    const imu_quaternionf first_increment =
        imu_quaternion_from_rotation_vector((imu_rotation_vector_rad){
            .x_rad = 0.1f,
            .y_rad = 0.0f,
            .z_rad = 0.0f,
        });
    const imu_quaternionf second_increment =
        imu_quaternion_from_rotation_vector((imu_rotation_vector_rad){
            .x_rad = 0.0f,
            .y_rad = 0.2f,
            .z_rad = 0.1f * 0.2f / 12.0f,
        });
    const imu_quaternionf expected = imu_quaternion_multiply(
        first_increment, second_increment);
    TEST_CHECK_NEAR_FLOAT(attitude.quaternion.w, expected.w, 1e-6f,
                          "two-sample coning scalar must match");
    TEST_CHECK_NEAR_FLOAT(attitude.quaternion.x, expected.x, 1e-6f,
                          "two-sample coning x must match");
    TEST_CHECK_NEAR_FLOAT(attitude.quaternion.y, expected.y, 1e-6f,
                          "two-sample coning y must match");
    TEST_CHECK_NEAR_FLOAT(attitude.quaternion.z, expected.z, 1e-6f,
                          "two-sample coning z must match");
}

static void test_stale_acceleration_has_zero_correction_weight(void) {
    imu_attitude attitude;
    imu_attitude_init(&attitude, &gated_test_config);
    const imu_attitude_acceleration fresh = {
        .acceleration_g = {0.0f, 0.5f, 0.8660254f},
        .valid = true,
        .fresh = true,
        .age_s = 0.0f,
    };
    const imu_attitude_acceleration stale = {
        .acceleration_g = fresh.acceleration_g,
        .valid = true,
        .fresh = false,
        .age_s = 0.001f,
    };

    (void)imu_attitude_update_timed(&attitude, test_zero_gyro, fresh, 0.01f);
    const imu_quaternionf after_fresh = attitude.quaternion;
    (void)imu_attitude_update_timed(&attitude, test_zero_gyro, stale, 0.01f);

    TEST_CHECK_NEAR_FLOAT(attitude.quaternion.w, after_fresh.w, 1e-6f,
                          "stale acceleration must not change scalar part");
    TEST_CHECK_NEAR_FLOAT(attitude.quaternion.x, after_fresh.x, 1e-6f,
                          "stale acceleration must not change x part");
    TEST_CHECK_NEAR_FLOAT(attitude.quaternion.y, after_fresh.y, 1e-6f,
                          "stale acceleration must not change y part");
    TEST_CHECK_NEAR_FLOAT(attitude.quaternion.z, after_fresh.z, 1e-6f,
                          "stale acceleration must not change z part");
}

static void test_high_rate_spin_suppresses_acceleration_correction(void) {
    imu_attitude gated;
    imu_attitude gyro_only;
    imu_attitude_init(&gated, &gated_test_config);
    imu_attitude_init(&gyro_only, &gated_test_config);
    const imu_acceleration_g tilted = {0.0f, 0.5f, 0.8660254f};
    const imu_acceleration_g free_fall = {0.0f, 0.0f, 0.0f};
    const imu_gyro_dps high_rate = {0.0f, 0.0f, 180.0f};

    const imu_attitude_acceleration fresh_tilted = {
        .acceleration_g = tilted,
        .valid = true,
        .fresh = true,
        .age_s = 0.0f,
    };
    const imu_attitude_acceleration invalid = {
        .acceleration_g = free_fall,
        .valid = false,
        .fresh = false,
        .age_s = 0.0f,
    };
    for (int step = 0; step < 100; step++) {
        (void)imu_attitude_update_timed(&gated, high_rate, fresh_tilted,
                                        0.01f);
        (void)imu_attitude_update_timed(&gyro_only, high_rate, invalid, 0.01f);
    }

    TEST_CHECK_NEAR_FLOAT(gated.quaternion.w, gyro_only.quaternion.w, 1e-5f,
                          "high-rate spin must suppress accel scalar correction");
    TEST_CHECK_NEAR_FLOAT(gated.quaternion.x, gyro_only.quaternion.x, 1e-5f,
                          "high-rate spin must suppress accel x correction");
    TEST_CHECK_NEAR_FLOAT(gated.quaternion.y, gyro_only.quaternion.y, 1e-5f,
                          "high-rate spin must suppress accel y correction");
    TEST_CHECK_NEAR_FLOAT(gated.quaternion.z, gyro_only.quaternion.z, 1e-5f,
                          "high-rate spin must suppress accel z correction");
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
    test_quaternion_multiply_conjugate_returns_identity();
    test_constant_rate_rotation_uses_exact_exponential_map();
    test_tiny_angle_rotation_stays_finite_and_normalized();
    test_two_sample_coning_compensation_matches_rotation_vector();
    test_stale_acceleration_has_zero_correction_weight();
    test_high_rate_spin_suppresses_acceleration_correction();
    return test_support_result();
}
