/* Initialization sequence tests for the BMI088 gyroscope driver.
 *
 * These lock the byte-exact power-up sequence against the fake bus: the
 * double chip-ID read, the soft reset and its settling delay, and the
 * write-then-readback of range, bandwidth, and power. They also prove the
 * driver never touches accelerometer or data-ready registers.
 */
#include "bmi088_gyro_fake_bus.h"
#include "test_support.h"

/* Expected register addresses, kept local so a driver-side typo cannot
 * silently agree with the test. */
#define REG_CHIP_ID 0x00
#define REG_RANGE 0x0F
#define REG_BANDWIDTH 0x10
#define REG_POWER 0x11
#define REG_SOFTRESET 0x14
#define REG_INT_CTRL 0x15
#define REG_INT3_INT4 0x16
#define REG_INT_MAP 0x18

static void test_init_issues_exact_sequence(void) {
    /* Given: a healthy gyro reporting the expected chip ID. */
    bmi088_fake_bus fake;
    fake_bus_init(&fake);
    bmi088_gyro_bus bus = fake_bus_make(&fake);
    bmi088_gyro dev;

    /* When: the driver initializes the device. */
    const bmi088_gyro_status status = bmi088_gyro_init(&dev, &bus);

    /* Then: init succeeds and the bus operations are byte-exact. */
    TEST_CHECK_EQ_INT(status, BMI088_GYRO_OK, "init must succeed");
    TEST_CHECK(fake.overflowed == 0, "op record must not overflow");

    /* Two chip-ID reads separated by a 1 ms settle. */
    TEST_CHECK_EQ_INT(fake.ops[0].kind, FAKE_OP_READ, "op0 must be a read");
    TEST_CHECK_EQ_INT(fake.ops[0].addr, REG_CHIP_ID, "op0 must read chip ID");
    TEST_CHECK_EQ_INT(fake.ops[1].kind, FAKE_OP_DELAY, "op1 must be a delay");
    TEST_CHECK_EQ_INT(fake.ops[1].delay_ms, 1, "op1 must settle 1 ms");
    TEST_CHECK_EQ_INT(fake.ops[2].kind, FAKE_OP_READ, "op2 must be a read");
    TEST_CHECK_EQ_INT(fake.ops[2].addr, REG_CHIP_ID, "op2 must read chip ID");
    TEST_CHECK_EQ_INT(fake.ops[2].value, 0x0F, "op2 must see ID 0x0F");

    /* Soft reset, then the mandatory settling delay. */
    TEST_CHECK_EQ_INT(fake.ops[3].kind, FAKE_OP_WRITE, "op3 must be a write");
    TEST_CHECK_EQ_INT(fake.ops[3].addr, REG_SOFTRESET, "op3 must hit 0x14");
    TEST_CHECK_EQ_INT(fake.ops[3].value, 0xB6, "op3 must write 0xB6");
    TEST_CHECK_EQ_INT(fake.ops[4].kind, FAKE_OP_DELAY, "op4 must be a delay");
    TEST_CHECK_EQ_INT(fake.ops[4].delay_ms, 80, "op4 must settle 80 ms");

    /* Chip ID is re-read after reset. */
    TEST_CHECK_EQ_INT(fake.ops[5].kind, FAKE_OP_READ, "op5 must be a read");
    TEST_CHECK_EQ_INT(fake.ops[5].addr, REG_CHIP_ID, "op5 must re-read ID");

    /* Each configuration register is written, settled, then read back.
     * 0x82 selects 1000 Hz ODR with the 116 Hz filter bandwidth; bit 7 is
     * a reserved-set bit, so 0x02 would be the wrong value. */
    const uint8_t cfg_addr[3] = {REG_RANGE, REG_BANDWIDTH, REG_POWER};
    const uint8_t cfg_value[3] = {0x00, 0x82, 0x00};
    for (int i = 0; i < 3; i++) {
        const uint8_t base = (uint8_t)(6 + (i * 3));
        TEST_CHECK_EQ_INT(fake.ops[base].kind, FAKE_OP_WRITE,
                          "config step must start with a write");
        TEST_CHECK_EQ_INT(fake.ops[base].addr, cfg_addr[i],
                          "config write must target the expected register");
        TEST_CHECK_EQ_INT(fake.ops[base].value, cfg_value[i],
                          "config write must carry the expected value");
        TEST_CHECK_EQ_INT(fake.ops[base + 1].kind, FAKE_OP_DELAY,
                          "config write must be followed by a delay");
        TEST_CHECK_EQ_INT(fake.ops[base + 1].delay_ms, 1,
                          "config settle must be 1 ms");
        TEST_CHECK_EQ_INT(fake.ops[base + 2].kind, FAKE_OP_READ,
                          "config step must end with a readback");
        TEST_CHECK_EQ_INT(fake.ops[base + 2].addr, cfg_addr[i],
                          "readback must target the written register");
        TEST_CHECK_EQ_INT(fake.ops[base + 2].value, cfg_value[i],
                          "readback must observe the written value");
    }

    /* Then: the bandwidth byte is pinned by value, not only via the table
     * above, so a wrong ODR/bandwidth selection fails by name. */
    TEST_CHECK_EQ_INT(fake.ops[9].addr, REG_BANDWIDTH,
                      "op9 must write the bandwidth register");
    TEST_CHECK_EQ_INT(fake.ops[9].value, 0x82,
                      "bandwidth must be 0x82 (1000 Hz ODR, 116 Hz filter)");

    /* Exactly the sequence above, with nothing appended. */
    TEST_CHECK_EQ_INT(fake.op_count, 15, "init must issue 15 operations");
}

static void test_init_settles_at_least_80ms_and_balances_cs(void) {
    /* Given: a healthy gyro. */
    bmi088_fake_bus fake;
    fake_bus_init(&fake);
    bmi088_gyro_bus bus = fake_bus_make(&fake);
    bmi088_gyro dev;

    /* When: the driver initializes the device. */
    const bmi088_gyro_status status = bmi088_gyro_init(&dev, &bus);

    /* Then: reset settling meets the datasheet minimum. */
    TEST_CHECK_EQ_INT(status, BMI088_GYRO_OK, "init must succeed");
    TEST_CHECK(fake.total_delay_ms >= 80u, "total delay must be >= 80 ms");

    /* Then: every select is matched by a deselect, never nested. */
    TEST_CHECK_EQ_INT(fake.select_count, fake.deselect_count,
                      "chip select must be balanced");
    TEST_CHECK_EQ_INT(fake.selected, 0, "chip select must end deasserted");
    TEST_CHECK_EQ_INT(fake.unbalanced, 0, "chip select must never nest");
    TEST_CHECK(fake.select_count > 0u, "chip select must actually be used");
}

static void test_init_never_writes_accel_or_drdy_registers(void) {
    /* Given: a healthy gyro. */
    bmi088_fake_bus fake;
    fake_bus_init(&fake);
    bmi088_gyro_bus bus = fake_bus_make(&fake);
    bmi088_gyro dev;

    /* When: the driver initializes the device. */
    (void)bmi088_gyro_init(&dev, &bus);

    /* Then: no interrupt or data-ready register is ever written. */
    TEST_CHECK_EQ_INT(fake_bus_write_count(&fake, REG_INT_CTRL), 0,
                      "0x15 must never be written");
    TEST_CHECK_EQ_INT(fake_bus_write_count(&fake, REG_INT3_INT4), 0,
                      "0x16 must never be written");
    TEST_CHECK_EQ_INT(fake_bus_write_count(&fake, REG_INT_MAP), 0,
                      "0x18 must never be written");

    /* Then: only the four documented registers are ever written. */
    for (uint8_t i = 0; i < fake.op_count; i++) {
        if (fake.ops[i].kind != FAKE_OP_WRITE) {
            continue;
        }
        const uint8_t addr = fake.ops[i].addr;
        const int allowed = (addr == REG_RANGE) || (addr == REG_BANDWIDTH) ||
                            (addr == REG_POWER) || (addr == REG_SOFTRESET);
        TEST_CHECK(allowed, "init must write only 0x0F/0x10/0x11/0x14");
    }
}

int main(void) {
    test_init_issues_exact_sequence();
    test_init_settles_at_least_80ms_and_balances_cs();
    test_init_never_writes_accel_or_drdy_registers();
    return test_support_result();
}
