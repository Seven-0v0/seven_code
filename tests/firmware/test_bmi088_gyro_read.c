/* Burst-read and decode tests for the BMI088 gyroscope driver.
 *
 * The driver reads 0x00..0x07 in a single full-duplex transaction so the
 * chip ID travels alongside the rate data as a canary: if the ID byte is
 * wrong, the SPI framing slipped and the rate bytes cannot be trusted.
 */
#include "bmi088_gyro_fake_bus.h"
#include "test_support.h"

/* Brings a fake up through a successful init and clears the op record so
 * read assertions start from index zero. */
static void arrange_initialized(bmi088_fake_bus *fake, bmi088_gyro_bus *bus,
                                bmi088_gyro *dev) {
    fake_bus_init(fake);
    *bus = fake_bus_make(fake);
    const bmi088_gyro_status status = bmi088_gyro_init(dev, bus);
    TEST_CHECK_EQ_INT(status, BMI088_GYRO_OK, "arrange: init must succeed");
    /* Clear the whole record, including chip-select and delay counters, so
     * read assertions measure the read alone and not init's traffic. */
    fake->op_count = 0;
    fake->transfer_count = 0;
    fake->select_count = 0;
    fake->deselect_count = 0;
    fake->total_delay_ms = 0;
}

static void test_read_uses_one_transaction_without_dummy(void) {
    /* Given: an initialized gyro holding known raw counts. */
    bmi088_fake_bus fake;
    bmi088_gyro_bus bus;
    bmi088_gyro dev;
    arrange_initialized(&fake, &bus, &dev);
    const bmi088_fake_axes axes = {100, -200, 300};
    fake_bus_set_axes(&fake, axes);
    bmi088_gyro_sample sample = {0};

    /* When: one sample is read. */
    const bmi088_gyro_status status = bmi088_gyro_read(&dev, &sample);

    /* Then: exactly one transfer covering 0x00..0x07 plus the command byte
     * is issued, with no separate dummy read. */
    TEST_CHECK_EQ_INT(status, BMI088_GYRO_OK, "read must succeed");
    TEST_CHECK_EQ_INT(sample.x_raw, axes.x, "read must retain raw x counts");
    TEST_CHECK_EQ_INT(sample.y_raw, axes.y, "read must retain raw y counts");
    TEST_CHECK_EQ_INT(sample.z_raw, axes.z, "read must retain raw z counts");
    TEST_CHECK_EQ_INT(fake.op_count, 1, "read must issue one operation");
    TEST_CHECK_EQ_INT(fake.ops[0].kind, FAKE_OP_READ, "read must be a read");
    TEST_CHECK_EQ_INT(fake.ops[0].addr, 0x00, "read must start at 0x00");
    TEST_CHECK_EQ_INT(fake.ops[0].len, 9, "read must transfer 9 bytes");
    TEST_CHECK_EQ_INT(fake.select_count, 1, "read must select once");
    TEST_CHECK_EQ_INT(fake.deselect_count, 1, "read must deselect once");
    TEST_CHECK_EQ_INT(fake.unbalanced, 0, "read must keep CS balanced");
}

static void test_read_rejects_bad_chip_id_canary(void) {
    /* Given: an initialized gyro whose ID byte later reads wrong, the
     * signature of a slipped or desynchronized SPI frame. */
    bmi088_fake_bus fake;
    bmi088_gyro_bus bus;
    bmi088_gyro dev;
    arrange_initialized(&fake, &bus, &dev);
    const bmi088_fake_axes axes = {7, 8, 9};
    fake_bus_set_axes(&fake, axes);
    fake.regs[0x00] = 0x00;
    bmi088_gyro_sample sample = {
        .x_mdps = 11,
        .y_mdps = 22,
        .z_mdps = 33,
    };

    /* When: a sample is read. */
    const bmi088_gyro_status status = bmi088_gyro_read(&dev, &sample);

    /* Then: the canary fails and the previous sample is left untouched. */
    TEST_CHECK_EQ_INT(status, BMI088_GYRO_ERR_CHIP_ID,
                      "bad canary must report CHIP_ID");
    TEST_CHECK_EQ_INT(sample.x_mdps, 11, "x must be untouched on canary fail");
    TEST_CHECK_EQ_INT(sample.y_mdps, 22, "y must be untouched on canary fail");
    TEST_CHECK_EQ_INT(sample.z_mdps, 33, "z must be untouched on canary fail");
}

static void test_read_decodes_each_axis_lsb_first(void) {
    /* Given: raw bytes chosen so a byte-swap or axis-swap cannot pass. */
    bmi088_fake_bus fake;
    bmi088_gyro_bus bus;
    bmi088_gyro dev;
    arrange_initialized(&fake, &bus, &dev);
    /* 0x0102 = 258, 0x0304 = 772, 0x0506 = 1286 when decoded LSB first. */
    fake.regs[0x02] = 0x02;
    fake.regs[0x03] = 0x01;
    fake.regs[0x04] = 0x04;
    fake.regs[0x05] = 0x03;
    fake.regs[0x06] = 0x06;
    fake.regs[0x07] = 0x05;
    bmi088_gyro_sample sample = {0};

    /* When: a sample is read. */
    const bmi088_gyro_status status = bmi088_gyro_read(&dev, &sample);

    /* Then: every axis decodes from its own little-endian pair. */
    TEST_CHECK_EQ_INT(status, BMI088_GYRO_OK, "read must succeed");
    TEST_CHECK_EQ_INT(sample.x_mdps, bmi088_gyro_raw_to_mdps(258),
                      "x must decode from 0x02/0x03 LSB first");
    TEST_CHECK_EQ_INT(sample.y_mdps, bmi088_gyro_raw_to_mdps(772),
                      "y must decode from 0x04/0x05 LSB first");
    TEST_CHECK_EQ_INT(sample.z_mdps, bmi088_gyro_raw_to_mdps(1286),
                      "z must decode from 0x06/0x07 LSB first");
}

static void test_read_decodes_signed_extremes(void) {
    /* Given: the full-scale and sign-boundary raw values. */
    const bmi088_fake_axes cases[5] = {{0, 0, 0},
                                       {1, 1, 1},
                                       {-1, -1, -1},
                                       {INT16_MAX, INT16_MAX, INT16_MAX},
                                       {INT16_MIN, INT16_MIN, INT16_MIN}};
    for (int i = 0; i < 5; i++) {
        bmi088_fake_bus fake;
        bmi088_gyro_bus bus;
        bmi088_gyro dev;
        arrange_initialized(&fake, &bus, &dev);
        fake_bus_set_axes(&fake, cases[i]);
        bmi088_gyro_sample sample = {0};

        /* When: a sample is read. */
        const bmi088_gyro_status status = bmi088_gyro_read(&dev, &sample);

        /* Then: the two's-complement value survives the round trip. */
        const int32_t expected = bmi088_gyro_raw_to_mdps(cases[i].x);
        TEST_CHECK_EQ_INT(status, BMI088_GYRO_OK, "extreme read must succeed");
        TEST_CHECK_EQ_INT(sample.x_mdps, expected, "x extreme must decode");
        TEST_CHECK_EQ_INT(sample.y_mdps, expected, "y extreme must decode");
        TEST_CHECK_EQ_INT(sample.z_mdps, expected, "z extreme must decode");
    }
}

static void test_read_leaves_sample_untouched_on_bus_failure(void) {
    /* Given: an initialized gyro and a previously valid sample. */
    bmi088_fake_bus fake;
    bmi088_gyro_bus bus;
    bmi088_gyro dev;
    arrange_initialized(&fake, &bus, &dev);
    const bmi088_fake_axes axes = {1000, 2000, 3000};
    fake_bus_set_axes(&fake, axes);
    bmi088_gyro_sample sample = {
        .x_mdps = -5,
        .y_mdps = -6,
        .z_mdps = -7,
    };

    /* When: the bus fails during the read transaction. */
    fake.fail_at_transfer = 0;
    const bmi088_gyro_status status = bmi088_gyro_read(&dev, &sample);

    /* Then: the caller keeps its last good sample instead of garbage. */
    TEST_CHECK_EQ_INT(status, BMI088_GYRO_ERR_BUS,
                      "read failure must report BUS");
    TEST_CHECK_EQ_INT(sample.x_mdps, -5, "x must survive a failed read");
    TEST_CHECK_EQ_INT(sample.y_mdps, -6, "y must survive a failed read");
    TEST_CHECK_EQ_INT(sample.z_mdps, -7, "z must survive a failed read");
    TEST_CHECK_EQ_INT(fake.selected, 0, "CS must be released on failure");
    TEST_CHECK_EQ_INT(fake.select_count, fake.deselect_count,
                      "CS must stay balanced on failure");
}

static void test_read_never_writes_any_register(void) {
    /* Given: an initialized gyro. */
    bmi088_fake_bus fake;
    bmi088_gyro_bus bus;
    bmi088_gyro dev;
    arrange_initialized(&fake, &bus, &dev);
    bmi088_gyro_sample sample = {0};

    /* When: several samples are read. */
    for (int i = 0; i < 3; i++) {
        (void)bmi088_gyro_read(&dev, &sample);
    }

    /* Then: reading is side-effect free on the device. */
    for (uint8_t i = 0; i < fake.op_count; i++) {
        TEST_CHECK(fake.ops[i].kind == FAKE_OP_READ,
                   "read path must issue reads only");
    }
}

int main(void) {
    test_read_uses_one_transaction_without_dummy();
    test_read_rejects_bad_chip_id_canary();
    test_read_decodes_each_axis_lsb_first();
    test_read_decodes_signed_extremes();
    test_read_leaves_sample_untouched_on_bus_failure();
    test_read_never_writes_any_register();
    return test_support_result();
}
