/* Host tests for the unsigned fixed-point ASCII integer formatter.
 *
 * fmt_u32 exists because telemetry counters (sequence numbers, millisecond
 * timestamps, error counts) are genuinely unsigned. Formatting them through
 * the signed path costs correctness: a uint32 past INT32_MAX reappears as a
 * negative number, which is how a 24.9-day uptime turned into a negative
 * timestamp. These tests pin the whole unsigned range, not just the half
 * that happens to fit in an int32.
 */
#include <stdint.h>
#include <string.h>

#include "fmt_i32.h"
#include "test_support.h"

#define SENTINEL '#'

/* Fills a buffer with a byte the formatter must never write, so any stray
 * write (including a NUL terminator) shows up as a failed comparison. */
static void fill_sentinel(char *buffer, size_t size) {
    memset(buffer, SENTINEL, size);
}

/* Checks that formatting value yields exactly expected, with no trailing NUL
 * and no byte written past the returned length. */
static void expect_formats_to(uint32_t value, const char *expected) {
    /* Given: a sentinel-filled buffer wider than the expected output. */
    char buffer[32];
    fill_sentinel(buffer, sizeof(buffer));
    const size_t expected_length = strlen(expected);

    /* When: the value is formatted with ample capacity. */
    const uint32_t written = fmt_u32(buffer, (uint32_t)sizeof(buffer), value);

    /* Then: the length is the digit count and the digits match exactly. */
    TEST_CHECK_EQ_INT(written, (long)expected_length,
                      "fmt_u32 must return the number of bytes written");
    TEST_CHECK(memcmp(buffer, expected, expected_length) == 0,
               "fmt_u32 must write the expected decimal digits");

    /* Then: nothing is written past the reported length, so no NUL. */
    TEST_CHECK(buffer[expected_length] == SENTINEL,
               "fmt_u32 must not write a terminator past the digits");
}

/* Confirms an undersized buffer is rejected and left exactly as it was. */
static void expect_rejects_capacity(uint32_t value, uint32_t capacity) {
    /* Given: a sentinel-filled buffer and a capacity too small to fit. */
    char buffer[32];
    char untouched[32];
    fill_sentinel(buffer, sizeof(buffer));
    fill_sentinel(untouched, sizeof(untouched));

    /* When: the value is formatted into that too-small capacity. */
    const uint32_t written = fmt_u32(buffer, capacity, value);

    /* Then: the call reports failure and writes nothing at all. */
    TEST_CHECK_EQ_INT(written, 0,
                      "fmt_u32 must return 0 when capacity is insufficient");
    TEST_CHECK(memcmp(buffer, untouched, sizeof(buffer)) == 0,
               "fmt_u32 must leave the buffer untouched on failure");
}

int main(void) {
    /* Zero is its own case: one digit, never a sign. */
    expect_formats_to(0u, "0");

    /* Ordinary magnitudes across the digit-count boundaries. */
    expect_formats_to(7u, "7");
    expect_formats_to(12345u, "12345");

    /* The value that motivated this formatter: one past INT32_MAX. Through
     * the signed path this is -2147483648, which is exactly the negative
     * timestamp the review found. */
    expect_formats_to(2147483648u, "2147483648");

    /* Both ends of the unsigned range. */
    expect_formats_to((uint32_t)INT32_MAX, "2147483647");
    expect_formats_to(UINT32_MAX, "4294967295");

    /* Exact-fit capacity must succeed: 10 bytes for "4294967295", which is
     * also FMT_U32_MAX_LEN, so a caller sizing from the constant is safe. */
    {
        char buffer[FMT_U32_MAX_LEN];
        fill_sentinel(buffer, sizeof(buffer));
        const uint32_t written =
            fmt_u32(buffer, FMT_U32_MAX_LEN, UINT32_MAX);
        TEST_CHECK_EQ_INT(written, 10, "exact-fit capacity must succeed");
        TEST_CHECK(memcmp(buffer, "4294967295", 10U) == 0,
                   "exact-fit output must be correct");
    }

    /* One byte short, for a value of every width, plus zero capacity. */
    expect_rejects_capacity(UINT32_MAX, 9U);
    expect_rejects_capacity(2147483648u, 9U);
    expect_rejects_capacity(12345u, 4U);
    expect_rejects_capacity(0u, 0U);

    return test_support_result();
}
