#include <stdint.h>

#include "gyro_snapshot.h"
#include "test_support.h"

static gyro_snapshot_payload payload(uint32_t sequence) {
    gyro_snapshot_payload value = {
        .sequence = sequence,
        .init_status = 0,
        .read_error_count = 3u,
        .last_read_status = BMI088_GYRO_ERR_BUS,
        .x_mdps = 10,
        .y_mdps = -20,
        .z_mdps = 30,
    };
    return value;
}

int main(void) {
    /* Given: a stable even generation and a complete payload. */
    volatile gyro_snapshot snapshot = {0};
    gyro_snapshot_payload expected = payload(7u);

    /* When: the single writer publishes it. */
    gyro_snapshot_publish(&snapshot, &expected);

    /* Then: generation is even and a reader accepts the exact payload. */
    TEST_CHECK_EQ_INT(snapshot.generation, 2,
                      "first publication must finish at generation 2");
    gyro_snapshot_payload actual = {0};
    TEST_CHECK(gyro_snapshot_read(&snapshot, &actual),
               "stable even snapshot must be readable");
    TEST_CHECK_EQ_INT(actual.sequence, expected.sequence,
                      "reader must receive the published sequence");
    TEST_CHECK_EQ_INT(actual.init_status, expected.init_status,
                      "reader must receive the published init status");
    TEST_CHECK_EQ_INT(actual.read_error_count, expected.read_error_count,
                      "reader must receive the published error count");
    TEST_CHECK_EQ_INT(actual.y_mdps, expected.y_mdps,
                      "reader must receive the published axes");
    TEST_CHECK_EQ_INT(actual.last_read_status, expected.last_read_status,
                      "reader must receive the published read status");

    /* Given: a read failure followed by a successful sample publication. */
    expected.last_read_status = BMI088_GYRO_OK;
    expected.sequence = 8u;
    expected.x_mdps = 40;
    expected.y_mdps = -50;
    expected.z_mdps = 60;

    /* When: the writer publishes the recovery payload. */
    gyro_snapshot_publish(&snapshot, &expected);

    /* Then: one coherent read sees the recovered axes and OK status together. */
    TEST_CHECK(gyro_snapshot_read(&snapshot, &actual),
               "recovery snapshot must be readable");
    TEST_CHECK_EQ_INT(actual.sequence, 8,
                      "recovery must publish its sequence");
    TEST_CHECK_EQ_INT(actual.last_read_status, BMI088_GYRO_OK,
                      "recovery must clear the last read failure");
    TEST_CHECK_EQ_INT(actual.read_error_count, 3,
                      "recovery must preserve accumulated read errors");
    TEST_CHECK_EQ_INT(actual.x_mdps, 40,
                      "recovery must publish its x axis coherently");
    TEST_CHECK_EQ_INT(actual.y_mdps, -50,
                      "recovery must publish its y axis coherently");
    TEST_CHECK_EQ_INT(actual.z_mdps, 60,
                      "recovery must publish its z axis coherently");

    /* Given: an odd generation representing a writer in progress. */
    snapshot.generation = 3u;

    /* When/Then: readers reject it rather than accepting torn fields. */
    TEST_CHECK(!gyro_snapshot_read(&snapshot, &actual),
               "odd generation must be rejected");

    /* Given: the last even generation before uint32 wrap. */
    snapshot.generation = UINT32_MAX - 1u;
    expected = payload(UINT32_MAX);

    /* When: another payload is published across the wrap. */
    gyro_snapshot_publish(&snapshot, &expected);

    /* Then: zero is a valid stable even generation and data remains readable. */
    TEST_CHECK_EQ_INT(snapshot.generation, 0,
                      "generation wrap must preserve even stability");
    TEST_CHECK(gyro_snapshot_read(&snapshot, &actual),
               "wrapped stable snapshot must be readable");
    TEST_CHECK_EQ_INT(actual.sequence, UINT32_MAX,
                      "wrapped publication must preserve its payload");

    return test_support_result();
}
