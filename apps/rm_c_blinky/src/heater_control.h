#ifndef APPS_RM_C_BLINKY_HEATER_CONTROL_H
#define APPS_RM_C_BLINKY_HEATER_CONTROL_H

#include <stdbool.h>
#include <stdint.h>

#define HEATER_CONTROL_TARGET_DEGC 40.0f
#define HEATER_CONTROL_MAX_TEMPERATURE_DEGC 45.0f
#define HEATER_CONTROL_MAX_DUTY 4500u
#define HEATER_CONTROL_STABLE_WINDOW_S 10u
#define HEATER_CONTROL_MAX_SAMPLE_AGE_S 2.5f
#define HEATER_CONTROL_MAX_DT_S 2.0f

typedef enum {
    HEATER_CONTROL_DISABLED = 0,
    HEATER_CONTROL_WARMING,
    HEATER_CONTROL_STABLE,
    HEATER_CONTROL_FAULT,
} heater_control_state;

typedef enum {
    HEATER_CONTROL_FAULT_NONE = 0,
    HEATER_CONTROL_FAULT_SENSOR_INVALID,
    HEATER_CONTROL_FAULT_SENSOR_STALE,
    HEATER_CONTROL_FAULT_OVERTEMPERATURE,
    HEATER_CONTROL_FAULT_TIMING,
} heater_control_fault;

typedef struct {
    bool temperature_sampled;
    bool temperature_valid;
    float temperature_degc;
    float dt_s;
} heater_control_input;

typedef struct {
    heater_control_state state;
    heater_control_fault fault;
    uint16_t duty;
    float temperature_degc;
    float temperature_age_s;
    float stable_time_s;
    bool stability_just_reached;
} heater_control_output;

typedef struct {
    heater_control_state state;
    heater_control_fault fault;
    float integral;
    float temperature_degc;
    float temperature_age_s;
    float stable_time_s;
    uint16_t duty;
    bool enabled;
    bool has_temperature;
    bool stability_just_reached;
} heater_control;

void heater_control_init(heater_control *controller, bool enabled);
void heater_control_force_off(heater_control *controller,
                              heater_control_fault fault);
heater_control_output heater_control_step(heater_control *controller,
                                           const heater_control_input *input);

#endif
