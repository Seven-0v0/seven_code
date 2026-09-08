/* 1 kHz BMI088 IMU observation task. See gyro_task.h for the contract. */
#include "gyro_task.h"

#include "bmi088_accel.h"
#include "bmi088_gyro.h"
#include "board.h"
#include "heater_experiment.h"
#include "imu_pipeline.h"
#include "imu_pipeline_snapshot.h"

#define IMU_PERIOD_MS (1000u / IMU_PIPELINE_SAMPLE_RATE_HZ)
#define TEMPERATURE_DIVIDER IMU_PIPELINE_SAMPLE_RATE_HZ
#define ACCEL_DIVIDER (IMU_PIPELINE_SAMPLE_RATE_HZ / 100u)
/* This task keeps the whole pipeline state plus a ~256-byte snapshot payload
 * on its stack, and runs float math that the CM4F port saves lazily. At -O0
 * the locals alone exceed the old 2x minimal (1 KiB) stack, so the task needs
 * 8x minimal. Overflow here trips vApplicationStackOverflowHook, which turns
 * the LED off and parks the board. */
#define GYRO_TASK_STACK ((uint16_t)(configMINIMAL_STACK_SIZE * 8))
#define GYRO_TASK_PRIORITY 1u

volatile gyro_snapshot g_gyro_snapshot = {
    .generation = 0u,
    .payload = {
        .sequence = 0u,
        .init_status = GYRO_SNAPSHOT_INIT_PENDING,
        .read_error_count = 0u,
        .last_read_status = BMI088_GYRO_OK,
        .x_mdps = 0,
        .y_mdps = 0,
        .z_mdps = 0,
        .accel_init_status = GYRO_SNAPSHOT_ACCEL_INIT_PENDING,
        .last_accel_read_status = GYRO_SNAPSHOT_ACCEL_INIT_PENDING,
        .accel_read_error_count = 0u,
        .last_temperature_status = GYRO_SNAPSHOT_ACCEL_INIT_PENDING,
    },
};

static float elapsed_seconds_from_ticks(TickType_t current,
                                        TickType_t previous) {
    return (float)(current - previous) / (float)configTICK_RATE_HZ;
}

static uint32_t skipped_cycles_from_ticks(TickType_t current,
                                          TickType_t previous) {
    const TickType_t elapsed_ticks = current - previous;
    const TickType_t period_ticks = pdMS_TO_TICKS(IMU_PERIOD_MS);
    if (period_ticks == 0u || elapsed_ticks <= period_ticks) {
        return 0u;
    }
    return (uint32_t)(elapsed_ticks / period_ticks - 1u);
}

static void gyro_select(void *ctx) {
    (void)ctx;
    board_imu_gyro_select();
}

static void gyro_deselect(void *ctx) {
    (void)ctx;
    board_imu_gyro_deselect();
}

static void accel_select(void *ctx) {
    (void)ctx;
    board_imu_accel_select();
}

static void accel_deselect(void *ctx) {
    (void)ctx;
    board_imu_accel_deselect();
}

static int bus_transfer(void *ctx, const uint8_t *tx, uint8_t *rx,
                        uint16_t len) {
    (void)ctx;
    return board_imu_spi_transfer(tx, rx, len) ? 0 : 1;
}

static void bus_delay_ms(void *ctx, uint32_t ms) {
    (void)ctx;
    vTaskDelay(pdMS_TO_TICKS(ms));
}

static const bmi088_gyro_bus gyro_bus = {
    .ctx = NULL,
    .select = gyro_select,
    .deselect = gyro_deselect,
    .transfer = bus_transfer,
    .delay_ms = bus_delay_ms,
};

static const bmi088_accel_bus accel_bus = {
    .ctx = NULL,
    .select = accel_select,
    .deselect = accel_deselect,
    .transfer = bus_transfer,
    .delay_ms = bus_delay_ms,
};

static void publish_gyro_error(gyro_snapshot_payload *snapshot,
                               const heater_experiment *heater,
                               bmi088_gyro_status status) {
    snapshot->read_error_count++;
    snapshot->last_read_status = status;
    snapshot->gyro_sample_valid = false;
    snapshot->x_mdps = 0;
    snapshot->y_mdps = 0;
    snapshot->z_mdps = 0;
    snapshot->temperature_valid = false;
    snapshot->sample_stationary = false;
    heater_experiment_snapshot_apply(snapshot, heater);
    gyro_snapshot_publish(&g_gyro_snapshot, snapshot);
}

static void publish_unpaired_cycle_evidence(gyro_snapshot_payload *snapshot,
                                            float cycle_dt_s,
                                            uint32_t skipped_cycles) {
    snapshot->sample_dt_s = cycle_dt_s;
    snapshot->skipped_cycles = skipped_cycles;
    snapshot->temperature_valid = false;
    snapshot->sample_stationary = false;
    snapshot->stack_high_water_words = uxTaskGetStackHighWaterMark(NULL);
}

static void gyro_task(void *context) {
    (void)context;

    bmi088_gyro gyro_device;
    bmi088_accel accel_device;
    imu_pipeline pipeline;
    heater_experiment heater;
    imu_pipeline_init(&pipeline);
    heater_experiment_init(&heater);
    imu_pipeline_set_calibration_enabled(
        &pipeline, heater_experiment_calibration_ready(&heater));

    const bmi088_gyro_status gyro_init_status =
        bmi088_gyro_init(&gyro_device, &gyro_bus);
    const bmi088_accel_status accel_init_status =
        bmi088_accel_init(&accel_device, &accel_bus);
    gyro_snapshot_payload snapshot = {0};
    snapshot.init_status = (int32_t)gyro_init_status;
    snapshot.accel_init_status = accel_init_status;
    snapshot.last_read_status = BMI088_GYRO_OK;
    snapshot.last_accel_read_status = GYRO_SNAPSHOT_ACCEL_INIT_PENDING;
    snapshot.last_temperature_status = GYRO_SNAPSHOT_ACCEL_INIT_PENDING;
    heater_experiment_snapshot_apply(&snapshot, &heater);
    gyro_snapshot_publish(&g_gyro_snapshot, &snapshot);

    if (gyro_init_status != BMI088_GYRO_OK ||
        accel_init_status != BMI088_ACCEL_OK) {
        heater_experiment_sensor_failure(&heater);
        heater_experiment_snapshot_apply(&snapshot, &heater);
        gyro_snapshot_publish(&g_gyro_snapshot, &snapshot);
        for (;;) {
            vTaskDelay(portMAX_DELAY);
        }
    }

    uint32_t sequence = 0u;
    bmi088_accel_sample accel_sample = {0};
    bool acceleration_valid = false;
    TickType_t wake_time = xTaskGetTickCount();
    TickType_t last_cycle_tick = wake_time;
    for (;;) {
        vTaskDelayUntil(&wake_time, pdMS_TO_TICKS(IMU_PERIOD_MS));
        const TickType_t cycle_tick = xTaskGetTickCount();
        const float cycle_dt_s =
            elapsed_seconds_from_ticks(cycle_tick, last_cycle_tick);
        const uint32_t skipped_cycles =
            skipped_cycles_from_ticks(cycle_tick, last_cycle_tick);
        last_cycle_tick = cycle_tick;
        bmi088_gyro_sample gyro_sample = {0};
        const bmi088_gyro_status gyro_status =
            bmi088_gyro_read(&gyro_device, &gyro_sample);
        if (gyro_status != BMI088_GYRO_OK) {
            heater_experiment_sensor_failure(&heater);
            const imu_pipeline_output output =
                imu_pipeline_advance_time(&pipeline, cycle_dt_s);
            imu_pipeline_snapshot_apply(&snapshot, &output);
            publish_unpaired_cycle_evidence(&snapshot, cycle_dt_s,
                                            skipped_cycles);
            publish_gyro_error(&snapshot, &heater, gyro_status);
            continue;
        }

        sequence++;
        snapshot.sequence = sequence;
        snapshot.last_read_status = BMI088_GYRO_OK;
        snapshot.gyro_sample_valid = true;
        snapshot.x_mdps = gyro_sample.x_mdps;
        snapshot.y_mdps = gyro_sample.y_mdps;
        snapshot.z_mdps = gyro_sample.z_mdps;

        if (sequence == 1u || (sequence % ACCEL_DIVIDER) == 0u) {
            const bmi088_accel_status accel_status =
                bmi088_accel_read(&accel_device, &accel_sample);
            snapshot.last_accel_read_status = accel_status;
            acceleration_valid = accel_status == BMI088_ACCEL_OK;
            if (acceleration_valid) {
                snapshot.accel_x_ug = accel_sample.x_ug;
                snapshot.accel_y_ug = accel_sample.y_ug;
                snapshot.accel_z_ug = accel_sample.z_ug;
            } else {
                snapshot.accel_read_error_count++;
                heater_experiment_sensor_failure(&heater);
            }
        }

        imu_pipeline_input input = {
            .gyro_raw = {
                .x = gyro_sample.x_raw,
                .y = gyro_sample.y_raw,
                .z = gyro_sample.z_raw,
            },
            .gyro_dps = {
                .x_dps = bmi088_gyro_raw_to_dps(gyro_sample.x_raw),
                .y_dps = bmi088_gyro_raw_to_dps(gyro_sample.y_raw),
                .z_dps = bmi088_gyro_raw_to_dps(gyro_sample.z_raw),
            },
            .acceleration_g = {
                .x_g = (float)accel_sample.x_ug / 1000000.0f,
                .y_g = (float)accel_sample.y_ug / 1000000.0f,
                .z_g = (float)accel_sample.z_ug / 1000000.0f,
            },
            .dt_s = cycle_dt_s,
            .skipped_cycles = skipped_cycles,
            .gyro_raw_valid = true,
            .acceleration_valid = acceleration_valid,
        };
        heater_control_input heater_input = {.dt_s = cycle_dt_s};

        if ((sequence % TEMPERATURE_DIVIDER) == 0u) {
            int32_t temperature_mdeg_c = snapshot.temperature_mdeg_c;
            const bmi088_accel_status temperature_status =
                bmi088_accel_read_temperature(&accel_device,
                                               &temperature_mdeg_c);
            snapshot.last_temperature_status = temperature_status;
            if (temperature_status == BMI088_ACCEL_OK) {
                snapshot.temperature_mdeg_c = temperature_mdeg_c;
                snapshot.temperature_read_count++;
                input.temperature_sampled = true;
                input.temperature_sample_valid = true;
                input.temperature_degc = (float)temperature_mdeg_c / 1000.0f;
                heater_input.temperature_sampled = true;
                heater_input.temperature_valid = true;
                heater_input.temperature_degc = input.temperature_degc;
            } else {
                snapshot.temperature_read_error_count++;
                input.temperature_sampled = true;
                input.temperature_sample_valid = false;
                heater_input.temperature_sampled = true;
                heater_input.temperature_valid = false;
            }
        }
        heater_experiment_step(&heater, &heater_input);
        if (heater.output.stability_just_reached) {
            imu_pipeline_set_calibration_enabled(&pipeline, true);
        }
        const imu_pipeline_output output =
            imu_pipeline_update_timed(&pipeline, &input);
        imu_pipeline_snapshot_apply(&snapshot, &output);
        heater_experiment_snapshot_apply(&snapshot, &heater);
        snapshot.stack_high_water_words = uxTaskGetStackHighWaterMark(NULL);
        gyro_snapshot_publish(&g_gyro_snapshot, &snapshot);
    }
}

BaseType_t gyro_task_create(void) {
    return xTaskCreate(gyro_task, "gyro", GYRO_TASK_STACK, NULL,
                       GYRO_TASK_PRIORITY, NULL);
}
