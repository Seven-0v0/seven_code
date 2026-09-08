#include "heater_experiment.h"

#include "board.h"

void heater_experiment_init(heater_experiment *experiment)
{
    heater_control_init(&experiment->controller,
                        BOARD_RM_DEV_BOARD_C_BMI088_HEATER_ENABLED != 0);
    experiment->output = (heater_control_output){
        .state = experiment->controller.state,
        .fault = experiment->controller.fault,
    };
}

void heater_experiment_sensor_failure(heater_experiment *experiment)
{
    heater_control_force_off(&experiment->controller,
                             HEATER_CONTROL_FAULT_SENSOR_INVALID);
    experiment->output = heater_control_step(
        &experiment->controller, &(heater_control_input){.dt_s = 0.02f});
    board_imu_heater_force_off();
}

void heater_experiment_step(heater_experiment *experiment,
                            const heater_control_input *input)
{
    experiment->output = heater_control_step(&experiment->controller, input);
    board_imu_heater_set_duty(experiment->output.duty);
}

bool heater_experiment_calibration_ready(const heater_experiment *experiment)
{
    return experiment->output.state == HEATER_CONTROL_DISABLED ||
           experiment->output.state == HEATER_CONTROL_STABLE;
}

void heater_experiment_snapshot_apply(gyro_snapshot_payload *snapshot,
                                      const heater_experiment *experiment)
{
    snapshot->heater_state = (uint32_t)experiment->output.state;
    snapshot->heater_fault = (uint32_t)experiment->output.fault;
    snapshot->heater_duty = experiment->output.duty;
    snapshot->heater_stable_time_s = experiment->output.stable_time_s;
}
