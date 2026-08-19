/* 50 Hz BMI088 gyroscope telemetry task. See gyro_task.h for the contract.
 *
 * This file is the only place that knows both the portable driver and this
 * board, so it is where the two are bound together. Output is built by
 * appending literals and integers into a fixed stack buffer -- no printf, no
 * snprintf, no floating point anywhere in the path.
 */
#include "gyro_task.h"

#include "bmi088_gyro.h"
#include "board.h"
#include "fmt_i32.h"

/* UART publishes at 50 Hz; the sensor remains configured at 1000 Hz. */
#define GYRO_PERIOD_MS 20u

/* A failing bus fails every cycle, so read errors are throttled to at most
 * one line per second to keep the UART readable. */
#define GYRO_ERROR_REPORT_INTERVAL_MS 1000u

/* Widest sample line is "G," + 10 digits + 4 separators + 3 * 11 digits +
 * CRLF, which is 61 bytes; the widest status line is shorter. 96 leaves
 * headroom without making the task stack interesting. */
#define GYRO_LINE_CAPACITY 96u

/* Bring-up needs an 80 ms settle plus formatting buffers on top of the
 * kernel minimum, hence double the minimal stack. Priority matches the
 * blink task: neither is more urgent than the other. */
#define GYRO_TASK_STACK ((uint16_t)(configMINIMAL_STACK_SIZE * 2))
#define GYRO_TASK_PRIORITY 1u

volatile gyro_snapshot g_gyro_snapshot = {
    .generation = 0u,
    .payload = {
        .sequence = 0u,
        .init_status = GYRO_SNAPSHOT_INIT_PENDING,
        .read_error_count = 0u,
        .x_mdps = 0,
        .y_mdps = 0,
        .z_mdps = 0,
    },
};

/* ---- Bus adapter: board API in, driver contract out -------------------- */

/* ctx is unused because this board has exactly one gyroscope and the pins
 * are fixed in board.h; the parameter exists for the driver's benefit. */
static void bus_select(void *ctx) {
    (void)ctx;
    board_imu_gyro_select();
}

static void bus_deselect(void *ctx) {
    (void)ctx;
    board_imu_gyro_deselect();
}

/* The board reports success as bool; the driver expects 0 for success and
 * non-zero for any bus error, so the sense is converted here. */
static int bus_transfer(void *ctx, const uint8_t *tx, uint8_t *rx,
                        uint16_t len) {
    (void)ctx;
    return board_imu_spi_transfer(tx, rx, len) ? 0 : 1;
}

/* Blocking delays yield to the scheduler, so the driver's 80 ms reset settle
 * costs the rest of the system nothing. */
static void bus_delay_ms(void *ctx, uint32_t ms) {
    (void)ctx;
    vTaskDelay(pdMS_TO_TICKS(ms));
}

static const bmi088_gyro_bus imu_bus = {
    .ctx = NULL,
    .select = bus_select,
    .deselect = bus_deselect,
    .transfer = bus_transfer,
    .delay_ms = bus_delay_ms,
};

/* ---- Line building ----------------------------------------------------- */

/* Append cursor over a caller-owned buffer. length is the bytes written so
 * far and is also the offset of the next write. */
typedef struct {
    char *out;
    uint32_t capacity;
    uint32_t length;
} line_writer;

static void append_text(line_writer *writer, const char *text) {
    while ((*text != '\0') && (writer->length < writer->capacity)) {
        writer->out[writer->length] = *text;
        writer->length++;
        text++;
    }
}

/* Appends a signed decimal. fmt_i32 writes nothing and returns 0 when the
 * remaining capacity is too small, which cannot happen here because
 * GYRO_LINE_CAPACITY covers the widest line every caller builds. */
static void append_i32(line_writer *writer, int32_t value) {
    writer->length += fmt_i32(&writer->out[writer->length],
                              writer->capacity - writer->length, value);
}

static void append_u32(line_writer *writer, uint32_t value) {
    writer->length += fmt_u32(&writer->out[writer->length],
                              writer->capacity - writer->length, value);
}

static void emit_uart(const line_writer *writer) {
    board_diagnostic_uart_write((const uint8_t *)writer->out,
                                (uint16_t)writer->length);
}

/* ---- Reporting -------------------------------------------------------- */

/* Quick Plot sample line: x_mdps,y_mdps,z_mdpsCRLF. */
static void report_sample(const bmi088_gyro_sample *sample) {
    char buffer[GYRO_LINE_CAPACITY];
    line_writer writer = {buffer, GYRO_LINE_CAPACITY, 0u};

    append_i32(&writer, sample->x_mdps);
    append_text(&writer, ",");
    append_i32(&writer, sample->y_mdps);
    append_text(&writer, ",");
    append_i32(&writer, sample->z_mdps);
    append_text(&writer, "\r\n");
    emit_uart(&writer);
}

/* Emitted once, immediately after a confirmed bring-up. The register values
 * are the ones the driver writes and reads back, restated here so a log
 * reader can tell which configuration produced the numbers that follow. */
static void report_ready(void) {
    static const uint8_t banner[] =
        "[GYRO] BMI088 gyro ready id=0x0F range=0x00 bw=0x82 pwr=0x00 "
        "rate=50Hz unit=mdps\r\n";
    board_diagnostic_uart_write(banner, sizeof(banner) - 1U);
    board_diagnostic_uart_write(banner, sizeof(banner) - 1U);
}

/* Compact numeric plus text status, so the line is both machine-parseable
 * and readable without the enum to hand. */
static void report_init_failure(bmi088_gyro_status status) {
    char buffer[GYRO_LINE_CAPACITY];
    line_writer writer = {buffer, GYRO_LINE_CAPACITY, 0u};

    append_text(&writer, "[GYRO] init failed status=");
    append_i32(&writer, (int32_t)status);
    append_text(&writer, " ");
    append_text(&writer, bmi088_gyro_status_text(status));
    append_text(&writer, "\r\n");
    emit_uart(&writer);
}

/* Read failure bookkeeping, kept together so the throttle state cannot drift
 * out of step with the count it throttles. */
typedef struct {
    uint32_t count;
    TickType_t last_report_tick;
} read_error_state;

static void note_read_error(read_error_state *state,
                            gyro_snapshot_payload *snapshot,
                            bmi088_gyro_status status) {
    state->count++;
    snapshot->read_error_count = state->count;
    gyro_snapshot_publish(&g_gyro_snapshot, snapshot);

    /* Unsigned tick subtraction is correct across the 32-bit tick wrap. */
    const TickType_t now = xTaskGetTickCount();
    if ((TickType_t)(now - state->last_report_tick) <
        pdMS_TO_TICKS(GYRO_ERROR_REPORT_INTERVAL_MS)) {
        return;
    }
    state->last_report_tick = now;

    char buffer[GYRO_LINE_CAPACITY];
    line_writer writer = {buffer, GYRO_LINE_CAPACITY, 0u};
    append_text(&writer, "[GYRO] read error status=");
    append_i32(&writer, (int32_t)status);
    append_text(&writer, " count=");
    append_u32(&writer, state->count);
    append_text(&writer, "\r\n");
    emit_uart(&writer);
}

/* Axis values are written before the sequence so a debugger that observes a
 * new sequence is looking at the sample that belongs to it. */
/* ---- Task ------------------------------------------------------------- */

static void gyro_task(void *context) {
    (void)context;

    /* Bring-up runs here, not in main(), because its reset settle must yield
     * to the scheduler instead of spinning with interrupts running. */
    bmi088_gyro device;
    const bmi088_gyro_status init_status = bmi088_gyro_init(&device, &imu_bus);
    gyro_snapshot_payload snapshot = {
        .sequence = 0u,
        .init_status = (int32_t)init_status,
        .read_error_count = 0u,
        .x_mdps = 0,
        .y_mdps = 0,
        .z_mdps = 0,
    };
    gyro_snapshot_publish(&g_gyro_snapshot, &snapshot);

    if (init_status != BMI088_GYRO_OK) {
        /* Only this task parks: the LED task keeps proving the board is
         * alive, and the snapshot keeps the cause readable over J-Link. */
        report_init_failure(init_status);
        for (;;) {
            vTaskDelay(portMAX_DELAY);
        }
    }
    report_ready();

    uint32_t sequence = 0u;

    /* Backdated one full interval so the first read failure, if any, is
     * reported immediately rather than a second late. */
    read_error_state errors = {
        0u, xTaskGetTickCount() -
                pdMS_TO_TICKS(GYRO_ERROR_REPORT_INTERVAL_MS)};

    TickType_t wake_time = xTaskGetTickCount();
    for (;;) {
        vTaskDelayUntil(&wake_time, pdMS_TO_TICKS(GYRO_PERIOD_MS));

        bmi088_gyro_sample sample = {0, 0, 0};
        const bmi088_gyro_status status = bmi088_gyro_read(&device, &sample);
        if (status != BMI088_GYRO_OK) {
            note_read_error(&errors, &snapshot, status);
            continue;
        }

        /* Published only on a confirmed read, so a reader never sees a
         * sequence advance over stale or partial axis values. */
        sequence++;
        snapshot.sequence = sequence;
        snapshot.x_mdps = sample.x_mdps;
        snapshot.y_mdps = sample.y_mdps;
        snapshot.z_mdps = sample.z_mdps;
        gyro_snapshot_publish(&g_gyro_snapshot, &snapshot);
        report_sample(&sample);
    }
}

BaseType_t gyro_task_create(void) {
    return xTaskCreate(gyro_task, "gyro", GYRO_TASK_STACK, NULL,
                       GYRO_TASK_PRIORITY, NULL);
}
