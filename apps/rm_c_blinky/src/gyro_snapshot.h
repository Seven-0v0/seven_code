/* Coherent gyroscope state publication for debugger and firmware readers. */
#ifndef APPS_RM_C_BLINKY_GYRO_SNAPSHOT_H
#define APPS_RM_C_BLINKY_GYRO_SNAPSHOT_H

#include <stdbool.h>
#include <stdint.h>

#define GYRO_SNAPSHOT_INIT_PENDING (-1)

typedef struct {
    uint32_t sequence;
    int32_t init_status;
    uint32_t read_error_count;
    int32_t x_mdps;
    int32_t y_mdps;
    int32_t z_mdps;
} gyro_snapshot_payload;

typedef struct {
    uint32_t generation;
    gyro_snapshot_payload payload;
} gyro_snapshot;

/* The single writer marks generation odd, writes the payload, then publishes
 * the next even generation. Aligned uint32 stores are atomic on Cortex-M4. */
void gyro_snapshot_publish(volatile gyro_snapshot *snapshot,
                           const gyro_snapshot_payload *payload);

/* Readers accept a copy only when generation is equal before and after the
 * payload read and is even. A false return means retry. */
bool gyro_snapshot_read(const volatile gyro_snapshot *snapshot,
                        gyro_snapshot_payload *payload);

#endif
