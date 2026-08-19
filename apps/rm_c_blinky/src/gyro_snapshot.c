#include "gyro_snapshot.h"

#define COMPILER_BARRIER() __asm volatile("" ::: "memory")

void gyro_snapshot_publish(volatile gyro_snapshot *snapshot,
                           const gyro_snapshot_payload *payload) {
    const uint32_t stable = snapshot->generation & ~1u;
    snapshot->generation = stable + 1u;
    COMPILER_BARRIER();
    snapshot->payload = *payload;
    COMPILER_BARRIER();
    snapshot->generation = stable + 2u;
}

bool gyro_snapshot_read(const volatile gyro_snapshot *snapshot,
                        gyro_snapshot_payload *payload) {
    const uint32_t before = snapshot->generation;
    if ((before & 1u) != 0u) {
        return false;
    }
    COMPILER_BARRIER();
    *payload = snapshot->payload;
    COMPILER_BARRIER();
    return snapshot->generation == before;
}
