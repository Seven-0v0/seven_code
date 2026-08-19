/* Fixed-point scale and status-text tests for the BMI088 gyro driver.
 *
 * The driver reports milli-degrees per second using pure integer math:
 * at the +/-2000 dps range one LSB is 2000/32768 dps, which is exactly
 * 15625/256 mdps. That ratio is representable, so the conversion needs no
 * floating point and stays bit-reproducible across compilers.
 */
#include <stdint.h>
#include <string.h>

#include "bmi088_gyro.h"
#include "test_support.h"

static void test_scale_is_exact_at_boundaries(void) {
    /* Given: the sign and full-scale boundary raw counts.
     * When: each is converted.
     * Then: the result equals raw * 15625 / 256 with C truncation toward
     *   zero, not a rounded or floating-point approximation. */
    TEST_CHECK_EQ_INT(bmi088_gyro_raw_to_mdps(0), 0, "zero must map to zero");
    TEST_CHECK_EQ_INT(bmi088_gyro_raw_to_mdps(1), 61,
                      "+1 LSB must be 61 mdps");
    TEST_CHECK_EQ_INT(bmi088_gyro_raw_to_mdps(-1), -61,
                      "-1 LSB must be -61 mdps");
    TEST_CHECK_EQ_INT(bmi088_gyro_raw_to_mdps(INT16_MAX), 1999938,
                      "INT16_MAX must be 1999938 mdps");
    TEST_CHECK_EQ_INT(bmi088_gyro_raw_to_mdps(INT16_MIN), -2000000,
                      "INT16_MIN must be exactly -2000000 mdps");
}

static void test_scale_matches_reference_formula(void) {
    /* Given: a sweep across the whole int16 domain.
     * When: each raw count is converted.
     * Then: the driver agrees with the reference expression everywhere,
     *   proving no intermediate overflow or sign-extension bug. */
    for (int32_t raw = INT16_MIN; raw <= INT16_MAX; raw += 257) {
        const int32_t expected = (int32_t)((raw * 15625) / 256);
        const int32_t actual = bmi088_gyro_raw_to_mdps((int16_t)raw);
        TEST_CHECK_EQ_INT(actual, expected,
                          "conversion must match raw * 15625 / 256");
    }
}

static void test_scale_is_monotonic_and_sign_symmetric(void) {
    /* Given: adjacent raw counts across zero.
     * When: converted.
     * Then: the mapping is strictly ordered and symmetric about zero, so a
     *   sign flip in the physical rate flips the reported sign. */
    int32_t previous = bmi088_gyro_raw_to_mdps(INT16_MIN);
    for (int32_t raw = INT16_MIN + 1; raw <= INT16_MAX; raw += 1023) {
        const int32_t current = bmi088_gyro_raw_to_mdps((int16_t)raw);
        TEST_CHECK(current > previous, "conversion must be monotonic");
        previous = current;
    }
    for (int16_t raw = 1; raw < 30000; raw = (int16_t)(raw + 4999)) {
        TEST_CHECK_EQ_INT(bmi088_gyro_raw_to_mdps((int16_t)(-raw)),
                          -bmi088_gyro_raw_to_mdps(raw),
                          "conversion must be symmetric about zero");
    }
}

static void test_status_text_is_total_and_distinct(void) {
    /* Given: every status the driver can return. */
    const bmi088_gyro_status all[6] = {
        BMI088_GYRO_OK,
        BMI088_GYRO_ERR_BUS,
        BMI088_GYRO_ERR_CHIP_ID,
        BMI088_GYRO_ERR_RANGE_READBACK,
        BMI088_GYRO_ERR_BANDWIDTH_READBACK,
        BMI088_GYRO_ERR_POWER_READBACK};

    /* When: each is rendered as text.
     * Then: it is non-empty and unique, so a log line names one cause. */
    for (int i = 0; i < 6; i++) {
        const char *text = bmi088_gyro_status_text(all[i]);
        TEST_CHECK(text != NULL, "status text must never be NULL");
        TEST_CHECK(text[0] != '\0', "status text must never be empty");
        for (int j = i + 1; j < 6; j++) {
            TEST_CHECK(strcmp(text, bmi088_gyro_status_text(all[j])) != 0,
                       "each status must render distinct text");
        }
    }
}

int main(void) {
    test_scale_is_exact_at_boundaries();
    test_scale_matches_reference_formula();
    test_scale_is_monotonic_and_sign_symmetric();
    test_status_text_is_total_and_distinct();
    return test_support_result();
}
