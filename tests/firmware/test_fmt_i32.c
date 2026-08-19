/* Host tests for the fixed-point ASCII integer formatter.
 *
 * The formatter exists so telemetry lines can be built without printf,
 * snprintf, or any float. These tests pin the three properties the caller
 * depends on: the exact digits, the returned length, and the promise that a
 * buffer too small is left byte-for-byte untouched.
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
static void expect_formats_to(int32_t value, const char *expected) {
    /* Given: a sentinel-filled buffer wider than the expected output. */
    char buffer[32];
    fill_sentinel(buffer, sizeof(buffer));
    const size_t expected_length = strlen(expected);

    /* When: the value is formatted with ample capacity. */
    const uint32_t written = fmt_i32(buffer, (uint32_t)sizeof(buffer), value);

    /* Then: the length is the digit count and the digits match exactly. */
    TEST_CHECK_EQ_INT(written, (long)expected_length,
                      "fmt_i32 must return the number of bytes written");
    TEST_CHECK(memcmp(buffer, expected, expected_length) == 0,
               "fmt_i32 must write the expected decimal digits");

    /* Then: nothing is written past the reported length, so no NUL. */
    TEST_CHECK(buffer[expected_length] == SENTINEL,
               "fmt_i32 must not write a terminator past the digits");
}

/* Confirms an undersized buffer is rejected and left exactly as it was. */
static void expect_rejects_capacity(int32_t value, uint32_t capacity) {
    /* Given: a sentinel-filled buffer and a capacity one byte too small. */
    char buffer[32];
    char untouched[32];
    fill_sentinel(buffer, sizeof(buffer));
    fill_sentinel(untouched, sizeof(untouched));

    /* When: the value is formatted into that too-small capacity. */
    const uint32_t written = fmt_i32(buffer, capacity, value);

    /* Then: the call reports failure and writes nothing at all. */
    TEST_CHECK_EQ_INT(written, 0,
                      "fmt_i32 must return 0 when capacity is insufficient");
    TEST_CHECK(memcmp(buffer, untouched, sizeof(buffer)) == 0,
               "fmt_i32 must leave the buffer untouched on failure");
}

int main(void) {
    /* Zero is its own case: one digit, no sign. */
    expect_formats_to(0, "0");

    /* Ordinary positive and negative magnitudes. */
    expect_formats_to(7, "7");
    expect_formats_to(12345, "12345");
    expect_formats_to(-42, "-42");

    /* Both extremes. INT32_MIN has no positive counterpart, so negating it
     * would be undefined behaviour; the formatter must handle it anyway. */
    expect_formats_to(INT32_MAX, "2147483647");
    expect_formats_to(INT32_MIN, "-2147483648");

    /* Exact-fit capacity must succeed: 11 bytes for "-2147483648". */
    {
        char buffer[11];
        fill_sentinel(buffer, sizeof(buffer));
        const uint32_t written = fmt_i32(buffer, 11U, INT32_MIN);
        TEST_CHECK_EQ_INT(written, 11, "exact-fit capacity must succeed");
        TEST_CHECK(memcmp(buffer, "-2147483648", 11U) == 0,
                   "exact-fit output must be correct");
    }

    /* One byte short, for a value of every shape, plus zero capacity. */
    expect_rejects_capacity(INT32_MIN, 10U);
    expect_rejects_capacity(-42, 2U);
    expect_rejects_capacity(12345, 4U);
    expect_rejects_capacity(0, 0U);

    return test_support_result();
}
