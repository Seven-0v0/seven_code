/* Minimal dependency-free host test support for firmware logic tests.
 *
 * No external test framework: plain C11 assertions with clear failure
 * messages. Each test source defines main() and reports pass/fail via
 * process exit code, which ctest checks directly.
 */
#ifndef FIRMWARE_TEST_SUPPORT_H
#define FIRMWARE_TEST_SUPPORT_H

#include <stdio.h>
#include <stdlib.h>

/* Tracks whether any check has failed within the current test binary. */
static int g_test_support_failed = 0;

#define TEST_CHECK(cond, msg)                                               \
    do {                                                                    \
        if (!(cond)) {                                                      \
            fprintf(stderr, "[FAIL] %s:%d: %s\n", __FILE__, __LINE__, msg); \
            g_test_support_failed = 1;                                      \
        }                                                                   \
    } while (0)

#define TEST_CHECK_EQ_INT(actual, expected, msg)                            \
    do {                                                                    \
        long test_actual_ = (long)(actual);                                 \
        long test_expected_ = (long)(expected);                             \
        if (test_actual_ != test_expected_) {                               \
            fprintf(stderr,                                                 \
                    "[FAIL] %s:%d: %s (actual=%ld expected=%ld)\n",         \
                    __FILE__, __LINE__, msg, test_actual_, test_expected_); \
            g_test_support_failed = 1;                                      \
        }                                                                   \
    } while (0)

/* Call once at the end of main(). Returns EXIT_SUCCESS or EXIT_FAILURE. */
static inline int test_support_result(void) {
    if (g_test_support_failed) {
        fprintf(stderr, "[RESULT] one or more checks failed\n");
        return EXIT_FAILURE;
    }
    fprintf(stdout, "[RESULT] all checks passed\n");
    return EXIT_SUCCESS;
}

#endif /* FIRMWARE_TEST_SUPPORT_H */
