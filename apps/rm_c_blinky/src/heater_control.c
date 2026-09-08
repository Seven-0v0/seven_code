#include "heater_control.h"

#include <math.h>

#define HEATER_CONTROL_PROPORTIONAL_GAIN 180.0f
#define HEATER_CONTROL_INTEGRAL_GAIN 8.0f
#define HEATER_CONTROL_INTEGRAL_LIMIT 4500.0f
#define HEATER_CONTROL_STABLE_BAND_DEGC 0.5f

static float clamp_float(float value, float minimum, float maximum) {
    if (value < minimum) {
        return minimum;
    }
    if (value > maximum) {
        return maximum;
    }
    return value;
}

static uint16_t bounded_duty(float value) {
    const float bounded = clamp_float(value, 0.0f,
                                      (float)HEATER_CONTROL_MAX_DUTY);
    return (uint16_t)bounded;
}

static heater_control_output output_from_controller(
    const heater_control *controller) {
    return (heater_control_output){
        .state = controller->state,
        .fault = controller->fault,
        .duty = controller->duty,
        .temperature_degc = controller->temperature_degc,
        .temperature_age_s = controller->temperature_age_s,
        .stable_time_s = controller->stable_time_s,
        .stability_just_reached = controller->stability_just_reached,
    };
}

static void latch_fault(heater_control *controller,
                        heater_control_fault fault) {
    controller->state = HEATER_CONTROL_FAULT;
    controller->fault = fault;
    controller->duty = 0u;
    controller->stability_just_reached = false;
}

static bool valid_dt(float dt_s) {
    return isfinite(dt_s) && dt_s > 0.0f && dt_s <= HEATER_CONTROL_MAX_DT_S;
}

static bool valid_temperature(float temperature_degc) {
    return isfinite(temperature_degc);
}

static bool stable_temperature(const heater_control *controller) {
    const float error = fabsf(controller->temperature_degc -
                              HEATER_CONTROL_TARGET_DEGC);
    return error <= HEATER_CONTROL_STABLE_BAND_DEGC;
}

static void update_control_from_temperature(heater_control *controller,
                                            float sample_dt_s) {
    const float error = HEATER_CONTROL_TARGET_DEGC -
                        controller->temperature_degc;
    const float proposed_integral = clamp_float(
        controller->integral + error * sample_dt_s *
                                  HEATER_CONTROL_INTEGRAL_GAIN,
        -HEATER_CONTROL_INTEGRAL_LIMIT, HEATER_CONTROL_INTEGRAL_LIMIT);
    const float requested_duty = error * HEATER_CONTROL_PROPORTIONAL_GAIN +
                                 proposed_integral;
    const uint16_t next_duty = bounded_duty(requested_duty);
    if (next_duty == 0u || next_duty < HEATER_CONTROL_MAX_DUTY || error < 0.0f) {
        controller->integral = proposed_integral;
    }
    controller->duty = next_duty;
    controller->stable_time_s = stable_temperature(controller)
                                    ? clamp_float(
                                          controller->stable_time_s + sample_dt_s,
                                          0.0f,
                                          (float)HEATER_CONTROL_STABLE_WINDOW_S)
                                    : 0.0f;
    if (controller->stable_time_s >=
        (float)HEATER_CONTROL_STABLE_WINDOW_S) {
        controller->stability_just_reached =
            controller->state != HEATER_CONTROL_STABLE;
        controller->state = HEATER_CONTROL_STABLE;
        return;
    }
    controller->state = HEATER_CONTROL_WARMING;
}

void heater_control_init(heater_control *controller, bool enabled) {
    *controller = (heater_control){
        .state = enabled ? HEATER_CONTROL_WARMING : HEATER_CONTROL_DISABLED,
        .enabled = enabled,
    };
}

void heater_control_force_off(heater_control *controller,
                              heater_control_fault fault) {
    if (controller->enabled) {
        latch_fault(controller, fault);
    }
}

heater_control_output heater_control_step(heater_control *controller,
                                           const heater_control_input *input) {
    controller->stability_just_reached = false;
    if (!controller->enabled) {
        controller->state = HEATER_CONTROL_DISABLED;
        controller->duty = 0u;
        return output_from_controller(controller);
    }
    if (controller->state == HEATER_CONTROL_FAULT) {
        controller->duty = 0u;
        return output_from_controller(controller);
    }
    if (!valid_dt(input->dt_s)) {
        latch_fault(controller, HEATER_CONTROL_FAULT_TIMING);
        return output_from_controller(controller);
    }

    controller->temperature_age_s =
        clamp_float(controller->temperature_age_s + input->dt_s, 0.0f,
                    HEATER_CONTROL_MAX_SAMPLE_AGE_S + input->dt_s);
    if (input->temperature_sampled) {
        if (!input->temperature_valid ||
            !valid_temperature(input->temperature_degc)) {
            latch_fault(controller, HEATER_CONTROL_FAULT_SENSOR_INVALID);
            return output_from_controller(controller);
        }
        if (input->temperature_degc >=
            HEATER_CONTROL_MAX_TEMPERATURE_DEGC) {
            latch_fault(controller, HEATER_CONTROL_FAULT_OVERTEMPERATURE);
            return output_from_controller(controller);
        }
        controller->temperature_degc = input->temperature_degc;
        const float sample_dt_s = controller->temperature_age_s;
        controller->temperature_age_s = 0.0f;
        controller->has_temperature = true;
        update_control_from_temperature(controller, sample_dt_s);
        return output_from_controller(controller);
    }
    if (controller->has_temperature &&
        controller->temperature_age_s > HEATER_CONTROL_MAX_SAMPLE_AGE_S) {
        latch_fault(controller, HEATER_CONTROL_FAULT_SENSOR_STALE);
        return output_from_controller(controller);
    }
    if (!controller->has_temperature) {
        controller->duty = 0u;
    }
    return output_from_controller(controller);
}
