/* Initialization failure-path tests for the BMI088 gyroscope driver.
 *
 * Every way power-up can go wrong must surface as a distinct status so a
 * caller can tell a miswired bus from a wrong part from a chip that
 * accepted a write but did not latch it.
 */
#include "bmi088_gyro_fake_bus.h"
#include "test_support.h"

#define REG_RANGE 0x0F
#define REG_BANDWIDTH 0x10
#define REG_POWER 0x11

static void test_init_rejects_wrong_chip_id(void) {
    /* Given: a device on the bus reporting the accelerometer ID, not the
     * gyro's 0x0F. */
    bmi088_fake_bus fake;
    fake_bus_init(&fake);
    fake.regs[0x00] = 0x1E;
    bmi088_gyro_bus bus = fake_bus_make(&fake);
    bmi088_gyro dev;

    /* When: the driver initializes the device. */
    const bmi088_gyro_status status = bmi088_gyro_init(&dev, &bus);

    /* Then: init fails with CHIP_ID and never resets the wrong part. */
    TEST_CHECK_EQ_INT(status, BMI088_GYRO_ERR_CHIP_ID,
                      "wrong chip ID must report CHIP_ID");
    TEST_CHECK_EQ_INT(fake_bus_write_count(&fake, 0x14), 0,
                      "a wrong part must never be soft reset");
}

static void test_init_reports_bus_failure(void) {
    /* Given: a bus whose very first transfer fails, as with a dead SPI
     * peripheral or a miswired MISO line. */
    bmi088_fake_bus fake;
    fake_bus_init(&fake);
    fake.fail_at_transfer = 0;
    bmi088_gyro_bus bus = fake_bus_make(&fake);
    bmi088_gyro dev;

    /* When: the driver initializes the device. */
    const bmi088_gyro_status status = bmi088_gyro_init(&dev, &bus);

    /* Then: init fails with BUS and releases the chip select. */
    TEST_CHECK_EQ_INT(status, BMI088_GYRO_ERR_BUS,
                      "transfer failure must report BUS");
    TEST_CHECK_EQ_INT(fake.selected, 0,
                      "chip select must be released on bus failure");
    TEST_CHECK_EQ_INT(fake.select_count, fake.deselect_count,
                      "chip select must stay balanced on bus failure");
}

static void test_init_reports_reset_write_failure(void) {
    /* Given: a bus that works for the ID reads but fails on the reset
     * write, which is transfer index 2. */
    bmi088_fake_bus fake;
    fake_bus_init(&fake);
    fake.fail_at_transfer = 2;
    bmi088_gyro_bus bus = fake_bus_make(&fake);
    bmi088_gyro dev;

    /* When: the driver initializes the device. */
    const bmi088_gyro_status status = bmi088_gyro_init(&dev, &bus);

    /* Then: init fails with BUS rather than masking it as a readback
     * mismatch. */
    TEST_CHECK_EQ_INT(status, BMI088_GYRO_ERR_BUS,
                       "reset write failure must report BUS");
    TEST_CHECK_EQ_INT(fake.unbalanced, 0,
                      "chip select must never nest on failure");
}

static void test_init_reports_post_reset_id_failure(void) {
    /* Given: reset succeeds but the following chip-ID transfer fails. */
    bmi088_fake_bus fake;
    fake_bus_init(&fake);
    fake.fail_at_transfer = 3;
    bmi088_gyro_bus bus = fake_bus_make(&fake);
    bmi088_gyro dev;

    /* When: initialization reaches the post-reset identity check. */
    const bmi088_gyro_status status = bmi088_gyro_init(&dev, &bus);

    /* Then: the transport failure remains a BUS error. */
    TEST_CHECK_EQ_INT(status, BMI088_GYRO_ERR_BUS,
                      "post-reset ID failure must report BUS");
    TEST_CHECK_EQ_INT(fake.select_count, fake.deselect_count,
                      "chip select must stay balanced after reset");
}

/* Each configuration register gets its own readback status, so a caller
 * learns which write the chip refused to latch. */
static void check_readback_mismatch(uint8_t stuck_reg,
                                    bmi088_gyro_status expected,
                                    const char *msg) {
    /* Given: a chip that silently drops writes to one config register. */
    bmi088_fake_bus fake;
    fake_bus_init(&fake);
    fake.stuck_reg = (int)stuck_reg;
    /* Seed a value that differs from what the driver will write, so the
     * dropped write is observable rather than accidentally correct. */
    fake.regs[stuck_reg] = 0x55;
    bmi088_gyro_bus bus = fake_bus_make(&fake);
    bmi088_gyro dev;

    /* When: the driver initializes the device. */
    const bmi088_gyro_status status = bmi088_gyro_init(&dev, &bus);

    /* Then: the mismatch is reported against the specific register. */
    TEST_CHECK_EQ_INT(status, expected, msg);
}

static void test_init_detects_readback_mismatches(void) {
    check_readback_mismatch(REG_RANGE, BMI088_GYRO_ERR_RANGE_READBACK,
                            "stuck range must report RANGE_READBACK");
    check_readback_mismatch(REG_BANDWIDTH, BMI088_GYRO_ERR_BANDWIDTH_READBACK,
                            "stuck bandwidth must report BANDWIDTH_READBACK");
    check_readback_mismatch(REG_POWER, BMI088_GYRO_ERR_POWER_READBACK,
                            "stuck power must report POWER_READBACK");
}

static void test_init_rechecks_chip_id_after_reset(void) {
    /* Given: a healthy chip whose reset register ignores writes in the fake. */
    bmi088_fake_bus fake;
    fake_bus_init(&fake);
    /* The soft-reset write is what flips the ID: pin 0x00 so the write to
     * 0x14 lands but the post-reset read sees garbage. */
    fake.stuck_reg = 0x14;
    fake.regs[0x14] = 0x00;
    bmi088_gyro_bus bus = fake_bus_make(&fake);
    bmi088_gyro dev;

    /* When: initialization proceeds through reset. */
    const bmi088_gyro_status status = bmi088_gyro_init(&dev, &bus);

    /* Then: init succeeds and the operation log proves chip ID is read again. */
    TEST_CHECK_EQ_INT(status, BMI088_GYRO_OK,
                      "a consistently healthy ID must pass both checks");
    TEST_CHECK(fake.op_count >= 6u, "post-reset ID must be re-read");
    TEST_CHECK_EQ_INT(fake.ops[5].addr, 0x00,
                      "op5 must re-read the chip ID register");
}

int main(void) {
    test_init_rejects_wrong_chip_id();
    test_init_reports_bus_failure();
    test_init_reports_reset_write_failure();
    test_init_reports_post_reset_id_failure();
    test_init_detects_readback_mismatches();
    test_init_rechecks_chip_id_after_reset();
    return test_support_result();
}
