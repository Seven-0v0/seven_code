#ifndef APPS_RM_C_BLINKY_HEATER_EXPERIMENT_H
#define APPS_RM_C_BLINKY_HEATER_EXPERIMENT_H

#include "heater_control.h"
#include "gyro_snapshot.h"

typedef struct {
    heater_control controller;
    heater_control_output output;
} heater_experiment;

void heater_experiment_init(heater_experiment *experiment);
void heater_experiment_sensor_failure(heater_experiment *experiment);
void heater_experiment_step(heater_experiment *experiment,
                            const heater_control_input *input);
bool heater_experiment_calibration_ready(const heater_experiment *experiment);
void heater_experiment_snapshot_apply(gyro_snapshot_payload *snapshot,
                                      const heater_experiment *experiment);

#endif
