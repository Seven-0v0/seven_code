#include "heater_control.h"
#include "test_support.h"

static heater_control_input sample(float temperature_degc, float dt_s) {
    return (heater_control_input){
        .temperature_sampled = true,
        .temperature_valid = true,
        .temperature_degc = temperature_degc,
        .dt_s = dt_s,
    };
}

static void test_disabled_controller_never_drives(void) {
    /* Given: the compile-time-disabled behavior is represented by an off
     * controller instance. */
    heater_control controller;
    heater_control_init(&controller, false);

    /* When: a valid temperature arrives. */
    const heater_control_input input = sample(25.0f, 1.0f);
    const heater_control_output output =
        heater_control_step(&controller, &input);

    /* Then: the state and command remain safely disabled. */
    TEST_CHECK_EQ_INT(output.state, HEATER_CONTROL_DISABLED,
                      "disabled heater must stay disabled");
    TEST_CHECK_EQ_INT(output.duty, 0u, "disabled heater duty must be zero");
    TEST_CHECK_EQ_INT(output.fault, HEATER_CONTROL_FAULT_NONE,
                      "disabled heater must not report a runtime fault");
}

static void test_valid_sample_starts_bounded_warming(void) {
    /* Given: an enabled controller with no trusted temperature yet. */
    heater_control controller;
    heater_control_init(&controller, true);

    /* When: the first valid one-hertz sample is accepted. */
    const heater_control_input input = sample(25.0f, 1.0f);
    const heater_control_output output =
        heater_control_step(&controller, &input);

    /* Then: it warms with a bounded, non-zero PWM command. */
    TEST_CHECK_EQ_INT(output.state, HEATER_CONTROL_WARMING,
                      "first valid sample must enter warming");
    TEST_CHECK(output.duty > 0u, "valid cold sample must request heat");
    TEST_CHECK(output.duty <= HEATER_CONTROL_MAX_DUTY,
               "warming duty must respect the hardware bound");
}

static void test_invalid_sample_latches_fail_off(void) {
    /* Given: the heater has already received a valid sample. */
    heater_control controller;
    heater_control_init(&controller, true);
    const heater_control_input initial = sample(25.0f, 1.0f);
    (void)heater_control_step(&controller, &initial);

    /* When: the scheduled sensor read fails. */
    const heater_control_input input = {
        .temperature_sampled = true,
        .temperature_valid = false,
        .dt_s = 1.0f,
    };
    const heater_control_output output =
        heater_control_step(&controller, &input);

    /* Then: the fault is latched and the command is immediately zero. */
    TEST_CHECK_EQ_INT(output.state, HEATER_CONTROL_FAULT,
                      "invalid temperature must fault");
    TEST_CHECK_EQ_INT(output.fault, HEATER_CONTROL_FAULT_SENSOR_INVALID,
                      "invalid temperature must identify the sensor fault");
    TEST_CHECK_EQ_INT(output.duty, 0u,
                      "invalid temperature must force heater off");
}

static void test_stale_sample_latches_fail_off(void) {
    /* Given: one valid temperature sample has enabled bounded heating. */
    heater_control controller;
    heater_control_init(&controller, true);
    const heater_control_input initial = sample(25.0f, 1.0f);
    (void)heater_control_step(&controller, &initial);

    /* When: no replacement sample arrives beyond the freshness limit. */
    heater_control_output output = {0};
    const heater_control_input missing = {.dt_s = 1.0f};
    for (uint32_t second = 0u; second < 3u; second++) {
        output = heater_control_step(&controller, &missing);
    }

    /* Then: stale evidence fails closed. */
    TEST_CHECK_EQ_INT(output.state, HEATER_CONTROL_FAULT,
                      "stale temperature must fault");
    TEST_CHECK_EQ_INT(output.fault, HEATER_CONTROL_FAULT_SENSOR_STALE,
                      "stale temperature must identify the age fault");
    TEST_CHECK_EQ_INT(output.duty, 0u,
                      "stale temperature must force heater off");
}

static void test_overtemperature_latches_fail_off(void) {
    /* Given: an enabled controller. */
    heater_control controller;
    heater_control_init(&controller, true);

    /* When: a sample reaches the independent thermal cutoff. */
    const heater_control_input input = sample(45.0f, 1.0f);
    const heater_control_output output =
        heater_control_step(&controller, &input);

    /* Then: overtemperature wins over the PI request. */
    TEST_CHECK_EQ_INT(output.state, HEATER_CONTROL_FAULT,
                      "cutoff temperature must fault");
    TEST_CHECK_EQ_INT(output.fault, HEATER_CONTROL_FAULT_OVERTEMPERATURE,
                      "cutoff must identify overtemperature");
    TEST_CHECK_EQ_INT(output.duty, 0u,
                      "overtemperature must force heater off");
}

static void test_stability_window_announces_once(void) {
    /* Given: repeated valid one-hertz samples at the 40 C target. */
    heater_control controller;
    heater_control_init(&controller, true);
    heater_control_output output = {0};

    /* When: the temperature remains in the stable window long enough. */
    for (uint32_t second = 0u;
         second < HEATER_CONTROL_STABLE_WINDOW_S + 1u; second++) {
        const heater_control_input input = sample(40.0f, 1.0f);
        output = heater_control_step(&controller, &input);
    }

    /* Then: stable is reached only after the window, and the edge is a pulse. */
    TEST_CHECK_EQ_INT(output.state, HEATER_CONTROL_STABLE,
                      "stable temperature must enter stable state");
    TEST_CHECK_NEAR_FLOAT(output.stable_time_s,
                          (float)HEATER_CONTROL_STABLE_WINDOW_S, 1e-5f,
                          "stable time must be bounded at the window");

    const heater_control_input input = sample(40.0f, 1.0f);
    output = heater_control_step(&controller, &input);
    TEST_CHECK(!output.stability_just_reached,
               "stability edge must be announced only once");
}

int main(void) {
    test_disabled_controller_never_drives();
    test_valid_sample_starts_bounded_warming();
    test_invalid_sample_latches_fail_off();
    test_stale_sample_latches_fail_off();
    test_overtemperature_latches_fail_off();
    test_stability_window_announces_once();
    return test_support_result();
}
