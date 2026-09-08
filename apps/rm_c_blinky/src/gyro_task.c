/* 50 Hz BMI088 IMU observation task. See gyro_task.h for the contract. */
#include "gyro_task.h"

#include "bmi088_accel.h"
#include "bmi088_gyro.h"
#include "board.h"
#include "imu_pipeline.h"

#define IMU_PERIOD_MS (1000u / IMU_PIPELINE_SAMPLE_RATE_HZ) /* 50 Hz cadence. */
#define TEMPERATURE_DIVIDER 50u /* 1 Hz temperature cadence at 50 Hz samples. */
/* This task keeps the whole pipeline state plus a ~256-byte snapshot payload
 * on its stack, and runs float math that the CM4F port saves lazily. At -O0
 * the locals alone exceed the old 2x minimal (1 KiB) stack, so the task needs
 * 4x minimal. Overflow here trips vApplicationStackOverflowHook, which turns
 * the LED off and parks the board. */
#define GYRO_TASK_STACK ((uint16_t)(configMINIMAL_STACK_SIZE * 4))
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
                               bmi088_gyro_status status) {
    snapshot->read_error_count++;
    snapshot->last_read_status = status;
    gyro_snapshot_publish(&g_gyro_snapshot, snapshot);
}

static void publish_pipeline_output(gyro_snapshot_payload *snapshot,
                                     const imu_pipeline_output *output) {
    snapshot->accel_x_g = output->acceleration_g.x_g;
    snapshot->accel_y_g = output->acceleration_g.y_g;
    snapshot->accel_z_g = output->acceleration_g.z_g;
    snapshot->calibrated_gyro_bias_x_dps = output->gyro_bias_dps.x_dps;
    snapshot->calibrated_gyro_bias_y_dps = output->gyro_bias_dps.y_dps;
    snapshot->calibrated_gyro_bias_z_dps = output->gyro_bias_dps.z_dps;
    snapshot->angular_velocity_x_dps = output->angular_velocity_dps.x_dps;
    snapshot->angular_velocity_y_dps = output->angular_velocity_dps.y_dps;
    snapshot->angular_velocity_z_dps = output->angular_velocity_dps.z_dps;
    snapshot->angular_acceleration_x_dps2 =
        output->angular_acceleration_dps2.x_dps2;
    snapshot->angular_acceleration_y_dps2 =
        output->angular_acceleration_dps2.y_dps2;
    snapshot->angular_acceleration_z_dps2 =
        output->angular_acceleration_dps2.z_dps2;
    snapshot->quaternion_w = output->attitude.quaternion.w;
    snapshot->quaternion_x = output->attitude.quaternion.x;
    snapshot->quaternion_y = output->attitude.quaternion.y;
    snapshot->quaternion_z = output->attitude.quaternion.z;
    snapshot->roll_deg = output->attitude.euler_zyx_deg.roll_deg;
    snapshot->pitch_deg = output->attitude.euler_zyx_deg.pitch_deg;
    snapshot->yaw_deg = output->attitude.euler_zyx_deg.yaw_deg;
    snapshot->calibration_accepted_samples =
        output->calibration_accepted_samples;
    snapshot->calibration_complete = output->calibration_complete;
    snapshot->stationary_samples = output->drift.stationary_samples;
    snapshot->stationary_duration_s = output->drift.stationary_duration_s;
    snapshot->drift_mean_x_dps = output->drift.mean_dps.x_dps;
    snapshot->drift_mean_y_dps = output->drift.mean_dps.y_dps;
    snapshot->drift_mean_z_dps = output->drift.mean_dps.z_dps;
    snapshot->drift_rms_x_dps = output->drift.rms_dps.x_dps;
    snapshot->drift_rms_y_dps = output->drift.rms_dps.y_dps;
    snapshot->drift_rms_z_dps = output->drift.rms_dps.z_dps;
    snapshot->yaw_drift_deg = output->drift.yaw_drift_deg;
    snapshot->yaw_drift_deg_per_min = output->drift.yaw_drift_deg_per_min;
}

static void gyro_task(void *context) {
    (void)context;

    bmi088_gyro gyro_device;
    bmi088_accel accel_device;
    imu_pipeline pipeline;
    imu_pipeline_init(&pipeline);

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
    gyro_snapshot_publish(&g_gyro_snapshot, &snapshot);

    if (gyro_init_status != BMI088_GYRO_OK) {
        for (;;) {
            vTaskDelay(portMAX_DELAY);
        }
    }

    const bool accel_ready = accel_init_status == BMI088_ACCEL_OK;
    uint32_t sequence = 0u;
    TickType_t wake_time = xTaskGetTickCount();
    for (;;) {
        vTaskDelayUntil(&wake_time, pdMS_TO_TICKS(IMU_PERIOD_MS));

        bmi088_gyro_sample gyro_sample = {0};
        const bmi088_gyro_status gyro_status =
            bmi088_gyro_read(&gyro_device, &gyro_sample);
        if (gyro_status != BMI088_GYRO_OK) {
            publish_gyro_error(&snapshot, gyro_status);
            continue;
        }

        sequence++;
        snapshot.sequence = sequence;
        snapshot.last_read_status = BMI088_GYRO_OK;
        snapshot.x_mdps = gyro_sample.x_mdps;
        snapshot.y_mdps = gyro_sample.y_mdps;
        snapshot.z_mdps = gyro_sample.z_mdps;

        if (!accel_ready) {
            gyro_snapshot_publish(&g_gyro_snapshot, &snapshot);
            continue;
        }

        bmi088_accel_sample accel_sample = {0};
        const bmi088_accel_status accel_status =
            bmi088_accel_read(&accel_device, &accel_sample);
        if (accel_status != BMI088_ACCEL_OK) {
            snapshot.accel_read_error_count++;
            snapshot.last_accel_read_status = accel_status;
            gyro_snapshot_publish(&g_gyro_snapshot, &snapshot);
            continue;
        }

        snapshot.last_accel_read_status = BMI088_ACCEL_OK;
        snapshot.accel_x_ug = accel_sample.x_ug;
        snapshot.accel_y_ug = accel_sample.y_ug;
        snapshot.accel_z_ug = accel_sample.z_ug;
        const imu_pipeline_output output = imu_pipeline_update(
            &pipeline,
            (imu_gyro_dps){
                .x_dps = (float)gyro_sample.x_mdps / 1000.0f,
                .y_dps = (float)gyro_sample.y_mdps / 1000.0f,
                .z_dps = (float)gyro_sample.z_mdps / 1000.0f,
            },
            (imu_acceleration_g){
                .x_g = (float)accel_sample.x_ug / 1000000.0f,
                .y_g = (float)accel_sample.y_ug / 1000000.0f,
                .z_g = (float)accel_sample.z_ug / 1000000.0f,
            });
        publish_pipeline_output(&snapshot, &output);

        if ((sequence % TEMPERATURE_DIVIDER) == 0u) {
            int32_t temperature_mdeg_c = snapshot.temperature_mdeg_c;
            const bmi088_accel_status temperature_status =
                bmi088_accel_read_temperature(&accel_device,
                                               &temperature_mdeg_c);
            snapshot.last_temperature_status = temperature_status;
            if (temperature_status == BMI088_ACCEL_OK) {
                snapshot.temperature_mdeg_c = temperature_mdeg_c;
                snapshot.temperature_read_count++;
            } else {
                snapshot.temperature_read_error_count++;
            }
        }
        gyro_snapshot_publish(&g_gyro_snapshot, &snapshot);
    }
}

BaseType_t gyro_task_create(void) {
    return xTaskCreate(gyro_task, "gyro", GYRO_TASK_STACK, NULL,
                       GYRO_TASK_PRIORITY, NULL);
}
