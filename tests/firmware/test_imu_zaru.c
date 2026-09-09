#include "imu_zaru.h"
#include "test_support.h"

static void test_stationary_window_bias_observer_converges(void) {
    const imu_zaru_config config = {
        .minimum_window_s = 0.1f,
        .minimum_samples = 100u,
        .max_window_stddev_dps = 0.05f,
        .bias_update_gain = 1.0f,
    };
    imu_zaru_observer observer;
    imu_zaru_init(&observer, &config);
    const imu_gyro_dps bias = {.x_dps = 0.3f, .y_dps = -0.2f, .z_dps = 0.1f};

    for (int sample = 0; sample < 1000; sample++) {
        const imu_zaru_input input = {
            .gyro_dps = bias,
            .dt_s = 0.001f,
            .sample_valid = true,
            .confirmed_stationary = true,
            .skipped_cycles = 0u,
        };
        (void)imu_zaru_update(&observer, &input);
    }

    TEST_CHECK_NEAR_FLOAT(observer.bias_dps.x_dps, bias.x_dps, 1e-3f,
                          "stationary ZARU must converge x bias");
    TEST_CHECK_NEAR_FLOAT(observer.bias_dps.y_dps, bias.y_dps, 1e-3f,
                          "stationary ZARU must converge y bias");
    TEST_CHECK_NEAR_FLOAT(observer.bias_dps.z_dps, bias.z_dps, 1e-3f,
                          "stationary ZARU must converge z bias");
}

static void test_bias_observer_holds_during_motion_and_skips(void) {
    const imu_zaru_config config = {
        .minimum_window_s = 0.1f,
        .minimum_samples = 10u,
        .max_window_stddev_dps = 0.05f,
        .bias_update_gain = 1.0f,
    };
    imu_zaru_observer observer;
    imu_zaru_init(&observer, &config);
    const imu_gyro_dps initial_bias = {.x_dps = 0.3f, .y_dps = 0.0f, .z_dps = 0.0f};
    imu_zaru_set_bias(&observer, initial_bias);
    const imu_zaru_input stationary = {
        .gyro_dps = initial_bias,
        .dt_s = 0.01f,
        .sample_valid = true,
        .confirmed_stationary = true,
        .skipped_cycles = 0u,
    };
    (void)imu_zaru_update(&observer, &stationary);
    const imu_gyro_dps held_bias = observer.bias_dps;

    const imu_zaru_input moving = {
        .gyro_dps = {.x_dps = 20.0f, .y_dps = 4.0f, .z_dps = -3.0f},
        .dt_s = 0.01f,
        .sample_valid = true,
        .confirmed_stationary = false,
        .skipped_cycles = 0u,
    };
    const imu_zaru_input skipped = {
        .gyro_dps = moving.gyro_dps,
        .dt_s = 0.01f,
        .sample_valid = true,
        .confirmed_stationary = true,
        .skipped_cycles = 1u,
    };
    const imu_zaru_input invalid = {
        .gyro_dps = moving.gyro_dps,
        .dt_s = 0.01f,
        .sample_valid = false,
        .confirmed_stationary = true,
        .skipped_cycles = 0u,
    };
    (void)imu_zaru_update(&observer, &moving);
    (void)imu_zaru_update(&observer, &skipped);
    (void)imu_zaru_update(&observer, &invalid);

    TEST_CHECK_NEAR_FLOAT(observer.bias_dps.x_dps, held_bias.x_dps, 0.0f,
                          "moving samples must not update ZARU x bias");
    TEST_CHECK_NEAR_FLOAT(observer.bias_dps.y_dps, held_bias.y_dps, 0.0f,
                          "moving samples must not update ZARU y bias");
    TEST_CHECK_NEAR_FLOAT(observer.bias_dps.z_dps, held_bias.z_dps, 0.0f,
                          "invalid samples must not update ZARU z bias");
    TEST_CHECK_EQ_INT(observer.window_sample_count, 0u,
                      "rejected evidence must clear the partial window");
}

static void test_noisy_window_requires_explicit_statistics_and_acceptance(void) {
    const imu_zaru_config config = {
        .minimum_window_s = 0.2f,
        .minimum_samples = 20u,
        .max_window_stddev_dps = 0.02f,
        .bias_update_gain = 1.0f,
    };
    imu_zaru_observer observer;
    imu_zaru_init(&observer, &config);
    for (uint32_t sample = 0u; sample < 20u; sample++) {
        const float noise = (sample & 1u) == 0u ? 0.01f : -0.01f;
        const imu_zaru_input input = {
            .gyro_dps = {
                .x_dps = 0.2f + noise,
                .y_dps = -0.1f - noise,
                .z_dps = 0.05f,
            },
            .dt_s = 0.01f,
            .sample_valid = true,
            .confirmed_stationary = true,
        };
        (void)imu_zaru_update(&observer, &input);
    }

    TEST_CHECK_EQ_INT(observer.accepted_window_count, 1u,
                      "a complete low-noise window must be accepted");
    TEST_CHECK_NEAR_FLOAT(observer.last_window_bias_dps.x_dps, 0.2f, 1e-5f,
                          "window statistics must report x mean");
    TEST_CHECK_NEAR_FLOAT(observer.last_window_bias_dps.y_dps, -0.1f, 1e-5f,
                          "window statistics must report y mean");
    TEST_CHECK(observer.last_window_stddev_dps < config.max_window_stddev_dps,
               "accepted window must report bounded noise");
    TEST_CHECK_EQ_INT(observer.last_window_sample_count, 20u,
                      "accepted window must report its sample count");
    TEST_CHECK_NEAR_FLOAT(observer.last_window_duration_s, 0.2f, 1e-6f,
                          "accepted window must report its duration");
    TEST_CHECK_NEAR_FLOAT(observer.bias_dps.z_dps, 0.05f, 1e-5f,
                          "accepted window mean must update the bias");
}

static void test_high_noise_window_is_rejected_without_bias_update(void) {
    const imu_zaru_config config = {
        .minimum_window_s = 0.1f,
        .minimum_samples = 10u,
        .max_window_stddev_dps = 0.01f,
        .bias_update_gain = 1.0f,
    };
    imu_zaru_observer observer;
    imu_zaru_init(&observer, &config);
    const imu_gyro_dps initial_bias = {.x_dps = 0.3f};
    imu_zaru_set_bias(&observer, initial_bias);
    for (uint32_t sample = 0u; sample < 11u; sample++) {
        const imu_zaru_input input = {
            .gyro_dps = {
                .x_dps = (sample & 1u) == 0u ? 0.0f : 0.2f,
            },
            .dt_s = 0.01f,
            .sample_valid = true,
            .confirmed_stationary = true,
        };
        (void)imu_zaru_update(&observer, &input);
    }

    TEST_CHECK_EQ_INT(observer.rejected_window_count, 1u,
                      "a noisy window must be rejected");
    TEST_CHECK_NEAR_FLOAT(observer.bias_dps.x_dps, initial_bias.x_dps, 0.0f,
                          "a noisy window must not change the bias");
}

int main(void) {
    test_stationary_window_bias_observer_converges();
    test_bias_observer_holds_during_motion_and_skips();
    test_noisy_window_requires_explicit_statistics_and_acceptance();
    test_high_noise_window_is_rejected_without_bias_update();
    return test_support_result();
}
