#include <stdint.h>

#include "bmi088_accel.h"
#include "test_support.h"

typedef struct {
    uint8_t registers[256];
    uint8_t last_tx[8];
    uint16_t last_length;
    uint32_t transfer_count;
    uint32_t select_count;
    uint32_t deselect_count;
    uint32_t delay_total_ms;
    uint8_t stuck_register;
    int fail_transfers;
} fake_accel_bus;

static void fake_select(void *context) {
    fake_accel_bus *fake = context;
    fake->select_count++;
}

static void fake_deselect(void *context) {
    fake_accel_bus *fake = context;
    fake->deselect_count++;
}

static int fake_transfer(void *context, const uint8_t *tx, uint8_t *rx,
                         uint16_t length) {
    fake_accel_bus *fake = context;
    fake->transfer_count++;
    fake->last_length = length;
    for (uint16_t index = 0u; index < length; index++) {
        fake->last_tx[index] = tx[index];
        rx[index] = 0u;
    }

    if (fake->fail_transfers != 0) {
        return -1;
    }

    const uint8_t register_address = (uint8_t)(tx[0] & 0x7Fu);
    if ((tx[0] & 0x80u) != 0u) {
        /* The second received byte is deliberately not the register value.
         * The third byte is the first real accelerometer response. */
        rx[1] = 0xD3u;
        for (uint16_t index = 2u; index < length; index++) {
            rx[index] = fake->registers[(uint8_t)(register_address + index - 2u)];
        }
    } else if (length >= 2u && register_address != fake->stuck_register) {
        fake->registers[register_address] = tx[1];
    }
    return 0;
}

static void fake_delay(void *context, uint32_t milliseconds) {
    fake_accel_bus *fake = context;
    fake->delay_total_ms += milliseconds;
}

static bmi088_accel_bus make_bus(fake_accel_bus *fake) {
    return (bmi088_accel_bus){
        .ctx = fake,
        .select = fake_select,
        .deselect = fake_deselect,
        .transfer = fake_transfer,
        .delay_ms = fake_delay,
    };
}

static void test_init_uses_accel_register_sequence(void) {
    fake_accel_bus fake = {0};
    fake.registers[0x00u] = 0x1Eu;
    bmi088_accel device;
    const bmi088_accel_bus bus = make_bus(&fake);

    const bmi088_accel_status status = bmi088_accel_init(&device, &bus);

    TEST_CHECK(status == BMI088_ACCEL_OK, "init must succeed");
    TEST_CHECK(fake.registers[0x7Eu] == 0xB6u, "init must soft reset");
    TEST_CHECK(fake.registers[0x7Du] == 0x04u,
               "init must enable acceleration");
    TEST_CHECK(fake.registers[0x7Cu] == 0x00u,
               "init must activate acceleration");
    TEST_CHECK(fake.registers[0x40u] == 0xA8u,
               "init must configure conservative ODR");
    TEST_CHECK(fake.registers[0x41u] == 0x02u,
               "init must configure the documented range");
    TEST_CHECK(fake.select_count == fake.deselect_count,
               "init must balance chip select");
    TEST_CHECK(fake.delay_total_ms >= 55u,
               "init must wait for reset and documented power-mode settles");
}

static void test_init_reports_readback_mismatch(void) {
    fake_accel_bus fake = {0};
    fake.registers[0x00u] = 0x1Eu;
    fake.stuck_register = 0x40u;
    bmi088_accel device;
    const bmi088_accel_bus bus = make_bus(&fake);

    const bmi088_accel_status status = bmi088_accel_init(&device, &bus);

    TEST_CHECK(status == BMI088_ACCEL_ERR_CONF_READBACK,
               "init must reject a refused configuration write");
}

static void test_init_rejects_wrong_chip_id(void) {
    fake_accel_bus fake = {0};
    fake.registers[0x00u] = 0x0Fu;
    bmi088_accel device;
    const bmi088_accel_bus bus = make_bus(&fake);

    const bmi088_accel_status status = bmi088_accel_init(&device, &bus);

    TEST_CHECK(status == BMI088_ACCEL_ERR_CHIP_ID,
               "init must reject a non-accelerometer ID");
    TEST_CHECK(fake.transfer_count == 2u,
               "wrong chip ID must stop after SPI wake-up and ID check");
}

static void test_read_proves_command_dummy_and_little_endian_data(void) {
    fake_accel_bus fake = {0};
    fake.registers[0x12u] = 0x34u;
    fake.registers[0x13u] = 0x12u;
    fake.registers[0x14u] = 0x00u;
    fake.registers[0x15u] = 0x80u;
    fake.registers[0x16u] = 0xFFu;
    fake.registers[0x17u] = 0x7Fu;
    bmi088_accel device = {0};
    device.bus = make_bus(&fake);
    bmi088_accel_sample sample = {1, 2, 3};

    const bmi088_accel_status status = bmi088_accel_read(&device, &sample);

    TEST_CHECK(status == BMI088_ACCEL_OK, "burst read must succeed");
    TEST_CHECK(fake.last_tx[0] == 0x92u,
               "burst command must address acceleration data");
    TEST_CHECK(fake.last_length == 8u,
               "burst must include command, dummy byte, and six data bytes");
    TEST_CHECK(sample.x_ug == bmi088_accel_raw_to_ug((int16_t)0x1234),
               "x must begin after the dummy byte");
    TEST_CHECK(sample.y_ug == bmi088_accel_raw_to_ug((int16_t)0x8000),
               "y must decode little endian after the dummy byte");
    TEST_CHECK(sample.z_ug == bmi088_accel_raw_to_ug((int16_t)0x7FFF),
               "z must decode little endian after the dummy byte");
}

static void test_raw_to_ug_uses_configured_12g_range(void) {
    TEST_CHECK(bmi088_accel_raw_to_ug(0) == 0,
               "zero raw counts must map to zero ug");
    TEST_CHECK(bmi088_accel_raw_to_ug(16384) == 6000000,
               "half scale must map to six g in ug");
    TEST_CHECK(bmi088_accel_raw_to_ug(-16384) == -6000000,
               "negative half scale must preserve sign");
    TEST_CHECK(bmi088_accel_raw_to_ug(INT16_MIN) == -12000000,
               "negative full scale must map to minus twelve g in ug");
}

static void test_accel_read_preserves_output_on_bus_failure(void) {
    fake_accel_bus fake = {0};
    fake.fail_transfers = 1;
    bmi088_accel device = {0};
    device.bus = make_bus(&fake);
    bmi088_accel_sample sample = {111, -222, 333};

    const bmi088_accel_status status = bmi088_accel_read(&device, &sample);

    TEST_CHECK(status == BMI088_ACCEL_ERR_BUS, "read must report bus failure");
    TEST_CHECK(sample.x_ug == 111, "failed read must retain x");
    TEST_CHECK(sample.y_ug == -222, "failed read must retain y");
    TEST_CHECK(sample.z_ug == 333, "failed read must retain z");
}

static void test_temperature_uses_bmi088_signed_eighth_degree_format(void) {
    fake_accel_bus fake = {0};
    /* Raw temperature 80 means 23 + 80 / 8 degrees C. */
    fake.registers[0x22u] = 0x0Au;
    fake.registers[0x23u] = 0x00u;
    bmi088_accel device = {0};
    device.bus = make_bus(&fake);
    int32_t temperature_mdeg_c = -1;

    const bmi088_accel_status status =
        bmi088_accel_read_temperature(&device, &temperature_mdeg_c);

    TEST_CHECK(status == BMI088_ACCEL_OK, "temperature read must succeed");
    TEST_CHECK(temperature_mdeg_c == 33000,
               "temperature must decode the signed 11-bit eighth-degree value");
    TEST_CHECK(fake.last_tx[0] == 0xA2u,
               "temperature command must use register 0x22");
    TEST_CHECK(fake.last_length == 4u,
               "temperature read must include command and dummy byte");
}

static void test_temperature_decodes_negative_11_bit_values(void) {
    fake_accel_bus fake = {0};
    fake.registers[0x22u] = 0xC1u;
    fake.registers[0x23u] = 0x00u;
    bmi088_accel device = {0};
    device.bus = make_bus(&fake);
    int32_t temperature_mdeg_c = 0;

    const bmi088_accel_status status =
        bmi088_accel_read_temperature(&device, &temperature_mdeg_c);

    TEST_CHECK(status == BMI088_ACCEL_OK,
               "negative temperature read must succeed");
    TEST_CHECK(temperature_mdeg_c == -40000,
               "temperature must sign-extend the 11-bit raw value");
}

static void test_temperature_read_preserves_output_on_failure(void) {
    fake_accel_bus fake = {0};
    fake.fail_transfers = 1;
    bmi088_accel device = {0};
    device.bus = make_bus(&fake);
    int32_t temperature_mdeg_c = 24500;

    const bmi088_accel_status status =
        bmi088_accel_read_temperature(&device, &temperature_mdeg_c);

    TEST_CHECK(status == BMI088_ACCEL_ERR_BUS,
               "temperature read must report bus failure");
    TEST_CHECK(temperature_mdeg_c == 24500,
               "failed temperature read must retain output");
}

static void test_status_text_is_stable(void) {
    TEST_CHECK(bmi088_accel_status_text(BMI088_ACCEL_OK) != NULL,
               "ok status must have text");
    TEST_CHECK(bmi088_accel_status_text(BMI088_ACCEL_ERR_BUS) != NULL,
               "bus status must have text");
    TEST_CHECK(bmi088_accel_status_text(BMI088_ACCEL_ERR_CHIP_ID) != NULL,
               "chip ID status must have text");
    TEST_CHECK(bmi088_accel_status_text(BMI088_ACCEL_ERR_CONF_READBACK) != NULL,
               "configuration status must have text");
}

int main(void) {
    test_init_uses_accel_register_sequence();
    test_init_reports_readback_mismatch();
    test_init_rejects_wrong_chip_id();
    test_read_proves_command_dummy_and_little_endian_data();
    test_raw_to_ug_uses_configured_12g_range();
    test_accel_read_preserves_output_on_bus_failure();
    test_temperature_uses_bmi088_signed_eighth_degree_format();
    test_temperature_decodes_negative_11_bit_values();
    test_temperature_read_preserves_output_on_failure();
    test_status_text_is_stable();
    return test_support_result();
}
