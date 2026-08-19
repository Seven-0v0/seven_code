/* Meta-test for the firmware host test harness itself.
 *
 * Given the test_support.h assertion helpers,
 * When a trivially true condition and an equal-integer comparison are
 *   checked,
 * Then test_support_result() reports success and the process exits 0,
 *   proving ctest can discover, build, and run a host test.
 */
#include "test_support.h"

int main(void) {
    /* Given: a known-good baseline condition. */
    const int given_value = 41 + 1;

    /* When: the harness checks a trivially true boolean condition. */
    TEST_CHECK(1 == 1, "harness boolean check must hold");

    /* When: the harness checks an integer equality. */
    TEST_CHECK_EQ_INT(given_value, 42, "harness integer equality must hold");

    /* Then: report the aggregate result as the process exit code. */
    return test_support_result();
}
