/* Fixed-point ASCII integer formatting implementation.
 *
 * Digits are generated least-significant first into a small stack scratch
 * buffer, then reversed into the caller's buffer only once the full length is
 * known. That ordering is what makes the "untouched on insufficient capacity"
 * guarantee free: nothing is written until the write is known to fit.
 */
#include "fmt_i32.h"

#define DECIMAL_BASE 10u

static uint32_t format_magnitude(char *out, uint32_t capacity,
                                 uint32_t magnitude, int negative) {
    char digits[FMT_I32_MAX_LEN];
    uint32_t digit_count = 0u;

    do {
        digits[digit_count] = (char)('0' + (char)(magnitude % DECIMAL_BASE));
        digit_count++;
        magnitude /= DECIMAL_BASE;
    } while (magnitude != 0u);

    const uint32_t length = digit_count + (negative ? 1u : 0u);
    if (capacity < length) {
        return 0u;
    }

    uint32_t at = 0u;
    if (negative) {
        out[at++] = '-';
    }
    while (digit_count > 0u) {
        out[at++] = digits[--digit_count];
    }
    return length;
}

uint32_t fmt_i32(char *out, uint32_t capacity, int32_t value) {
    const int negative = (value < 0);

    /* The magnitude is taken in unsigned space. Negating INT32_MIN as a
     * signed value is undefined behaviour because it has no positive int32
     * counterpart, while the unsigned subtraction below is exact for it. */
    uint32_t magnitude = negative ? (0u - (uint32_t)value) : (uint32_t)value;
    return format_magnitude(out, capacity, magnitude, negative);
}

uint32_t fmt_u32(char *out, uint32_t capacity, uint32_t value) {
    return format_magnitude(out, capacity, value, 0);
}
