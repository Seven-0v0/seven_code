/* In-memory fake SPI bus for the BMI088 gyroscope driver tests.
 *
 * This is a real fake, not a per-call return mock: it owns a 256-byte
 * register map with genuine BMI088 SPI semantics (bit 7 of the command
 * byte selects read vs write, reads shift out data one index later) and
 * records every operation the driver performs so tests can assert on the
 * exact byte sequence, chip-select balance, and cumulative delay.
 */
#ifndef BMI088_GYRO_FAKE_BUS_H
#define BMI088_GYRO_FAKE_BUS_H

#include <stdint.h>
#include <string.h>

#include "bmi088_gyro.h"

#define FAKE_BUS_REG_COUNT 256
#define FAKE_BUS_MAX_OPS 64
#define FAKE_BUS_NO_FAILURE (-1)
#define FAKE_BUS_NO_STUCK_REG (-1)

typedef enum {
    FAKE_OP_READ,
    FAKE_OP_WRITE,
    FAKE_OP_DELAY
} fake_op_kind;

/* One recorded driver operation. addr/value are meaningful for READ and
 * WRITE, delay_ms only for DELAY, len is the full transaction length. */
typedef struct {
    fake_op_kind kind;
    uint8_t addr;
    uint8_t value;
    uint16_t len;
    uint32_t delay_ms;
} fake_op;

typedef struct {
    uint8_t regs[FAKE_BUS_REG_COUNT];
    fake_op ops[FAKE_BUS_MAX_OPS];
    uint8_t op_count;
    uint8_t overflowed;

    /* Chip-select bookkeeping. */
    uint16_t select_count;
    uint16_t deselect_count;
    int selected;
    int unbalanced;

    uint32_t total_delay_ms;

    /* Failure injection: fail the Nth (0-based) transfer, or all of them. */
    int fail_at_transfer;
    int transfer_count;

    /* Readback mismatch injection: writes to this register are dropped. */
    int stuck_reg;
} bmi088_fake_bus;

/* Signed raw counts for the three gyro axes, as the chip would report. */
typedef struct {
    int16_t x;
    int16_t y;
    int16_t z;
} bmi088_fake_axes;

static inline void fake_bus_record(bmi088_fake_bus *fake, fake_op op) {
    if (fake->op_count >= FAKE_BUS_MAX_OPS) {
        fake->overflowed = 1;
        return;
    }
    fake->ops[fake->op_count] = op;
    fake->op_count++;
}

static inline void fake_bus_select(void *ctx) {
    bmi088_fake_bus *fake = (bmi088_fake_bus *)ctx;
    if (fake->selected) {
        fake->unbalanced = 1;
    }
    fake->selected = 1;
    fake->select_count++;
}

static inline void fake_bus_deselect(void *ctx) {
    bmi088_fake_bus *fake = (bmi088_fake_bus *)ctx;
    if (!fake->selected) {
        fake->unbalanced = 1;
    }
    fake->selected = 0;
    fake->deselect_count++;
}

static inline void fake_bus_delay_ms(void *ctx, uint32_t ms) {
    bmi088_fake_bus *fake = (bmi088_fake_bus *)ctx;
    fake_op op = {FAKE_OP_DELAY, 0u, 0u, 0u, ms};
    fake->total_delay_ms += ms;
    fake_bus_record(fake, op);
}

static inline int fake_bus_transfer(void *ctx, const uint8_t *tx, uint8_t *rx,
                                   uint16_t len) {
    bmi088_fake_bus *fake = (bmi088_fake_bus *)ctx;
    const int index = fake->transfer_count;
    fake->transfer_count++;

    if (!fake->selected) {
        fake->unbalanced = 1;
    }
    if (len < 2u || tx == NULL || rx == NULL) {
        return -1;
    }
    if (fake->fail_at_transfer == index) {
        return -1;
    }

    const uint8_t addr = (uint8_t)(tx[0] & 0x7Fu);
    const int is_read = (tx[0] & 0x80u) != 0u;

    memset(rx, 0, len);
    if (is_read) {
        /* Real BMI088 SPI reads: rx[0] is undefined, register data starts
         * one index later, and consecutive registers stream out. */
        for (uint16_t i = 1u; i < len; i++) {
            const uint16_t reg = (uint16_t)(addr + i - 1u);
            rx[i] = (reg < FAKE_BUS_REG_COUNT) ? fake->regs[reg] : 0u;
        }
        fake_op op = {FAKE_OP_READ, addr, rx[1], len, 0u};
        fake_bus_record(fake, op);
        return 0;
    }

    for (uint16_t i = 1u; i < len; i++) {
        const uint16_t reg = (uint16_t)(addr + i - 1u);
        if (reg < FAKE_BUS_REG_COUNT && (int)reg != fake->stuck_reg) {
            fake->regs[reg] = tx[i];
        }
    }
    fake_op op = {FAKE_OP_WRITE, addr, tx[1], len, 0u};
    fake_bus_record(fake, op);
    return 0;
}

/* Resets the fake to a healthy powered-up gyro reporting chip ID 0x0F. */
static inline void fake_bus_init(bmi088_fake_bus *fake) {
    memset(fake, 0, sizeof(*fake));
    fake->regs[0x00] = 0x0F;
    fake->fail_at_transfer = FAKE_BUS_NO_FAILURE;
    fake->stuck_reg = FAKE_BUS_NO_STUCK_REG;
}

static inline bmi088_gyro_bus fake_bus_make(bmi088_fake_bus *fake) {
    bmi088_gyro_bus bus = {fake, fake_bus_select, fake_bus_deselect,
                           fake_bus_transfer, fake_bus_delay_ms};
    return bus;
}

/* Loads signed raw counts into RATE_X/Y/Z (0x02..0x07), LSB first. */
static inline void fake_bus_set_axes(bmi088_fake_bus *fake,
                                     bmi088_fake_axes axes) {
    const uint16_t x = (uint16_t)axes.x;
    const uint16_t y = (uint16_t)axes.y;
    const uint16_t z = (uint16_t)axes.z;
    fake->regs[0x02] = (uint8_t)(x & 0xFFu);
    fake->regs[0x03] = (uint8_t)(x >> 8);
    fake->regs[0x04] = (uint8_t)(y & 0xFFu);
    fake->regs[0x05] = (uint8_t)(y >> 8);
    fake->regs[0x06] = (uint8_t)(z & 0xFFu);
    fake->regs[0x07] = (uint8_t)(z >> 8);
}

/* Counts recorded writes to a register, for "must never touch" assertions. */
static inline int fake_bus_write_count(const bmi088_fake_bus *fake,
                                       uint8_t addr) {
    int count = 0;
    for (uint8_t i = 0; i < fake->op_count; i++) {
        if (fake->ops[i].kind == FAKE_OP_WRITE && fake->ops[i].addr == addr) {
            count++;
        }
    }
    return count;
}

#endif /* BMI088_GYRO_FAKE_BUS_H */
