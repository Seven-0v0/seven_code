/* Fixed-point ASCII integer formatting for telemetry lines.
 *
 * Exists so firmware can build output lines without printf, snprintf, or any
 * floating point: no stdio, no locale, no hidden allocation, and a bounded
 * stack cost the caller can see. Portable by construction -- no HAL, no board
 * header, no RTOS -- so the same source compiles for the MCU target and for
 * the host test harness.
 */
#ifndef DRIVERS_UTIL_FMT_FMT_I32_H
#define DRIVERS_UTIL_FMT_FMT_I32_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Widest output fmt_i32 can produce: "-2147483648" is 11 bytes. A caller
 * sizing a buffer from this constant never needs a runtime capacity check. */
#define FMT_I32_MAX_LEN 11u
#define FMT_U32_MAX_LEN 10u

/* Writes value as signed decimal at out and returns the number of bytes
 * written.
 *
 * No terminator is appended, so the caller owns framing and may append
 * directly at out + return value. Returns 0 when capacity is too small for
 * the full number, and in that case out is left byte-for-byte untouched --
 * a partial number is never emitted. INT32_MIN is handled exactly.
 */
uint32_t fmt_i32(char *out, uint32_t capacity, int32_t value);

/* Unsigned counterpart for counters and timestamps. The full uint32 range is
 * preserved, including values above INT32_MAX. The output and capacity
 * contracts are identical to fmt_i32. */
uint32_t fmt_u32(char *out, uint32_t capacity, uint32_t value);

#ifdef __cplusplus
}
#endif

#endif /* DRIVERS_UTIL_FMT_FMT_I32_H */
