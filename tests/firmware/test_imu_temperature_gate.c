#include "imu_temperature_gate.h"
#include "test_support.h"

int main(void) {
    const imu_temperature_gate_config config = {
        .max_temperature_age_s = 2.5f,
        .max_temperature_slope_degc_per_s = 0.1f,
    };
    imu_temperature_gate gate;
    imu_temperature_gate_init(&gate, &config);

    /* Given: the first valid reading has no measurable slope yet. */
    /* When: it is recorded. */
    imu_temperature_gate_record(&gate, true, 25.0f);
    /* Then: refinement must wait for a second timestamped reading. */
    TEST_CHECK(!imu_temperature_gate_is_valid(&gate),
               "one temperature reading cannot prove thermal stability");

    /* Given: one actual second elapsed at a small measured slope. */
    /* When: the next valid reading is recorded. */
    imu_temperature_gate_advance(&gate, 1.0f);
    imu_temperature_gate_record(&gate, true, 25.05f);
    /* Then: the gate opens without inventing a thermal correction. */
    TEST_CHECK(imu_temperature_gate_is_valid(&gate),
               "a fresh stable temperature slope must permit refinement");
    TEST_CHECK_NEAR_FLOAT(gate.slope_degc_per_s, 0.05f, 1e-5f,
                          "slope must use the elapsed sample time");

    /* Given: a too-fast temperature change. */
    /* When: it arrives after another actual second. */
    imu_temperature_gate_advance(&gate, 1.0f);
    imu_temperature_gate_record(&gate, true, 25.30f);
    /* Then: the slope gate closes. */
    TEST_CHECK(!imu_temperature_gate_is_valid(&gate),
               "a temperature slope above the evidence limit must block refinement");

    /* Given: a read failure after a previously valid value. */
    /* When: the driver reports it. */
    imu_temperature_gate_record(&gate, false, 0.0f);
    /* Then: stale last-good temperature must not keep refinement enabled. */
    TEST_CHECK(!imu_temperature_gate_is_valid(&gate),
               "an invalid temperature read must close the refinement gate");

    return test_support_result();
}
