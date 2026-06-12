#ifndef TEST_RUNNER_H
#define TEST_RUNNER_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int tests_passed = 0;
static int tests_failed = 0;
static int assertions_passed = 0;
static int assertions_failed = 0;

#define TEST(name) do { \
    printf("  TEST: %s ... ", name); \
    fflush(stdout); \
    int _ok = 1;

#define END_TEST \
    if (_ok) { \
        tests_passed++; \
        printf("PASS\n"); \
    } else { \
        tests_failed++; \
        printf("FAIL\n"); \
    } \
} while (0)

#define ASSERT(cond, msg) do { \
    if (!(cond)) { \
        printf("\n    assertion failed: %s [%s:%d]", msg, __FILE__, __LINE__); \
        _ok = 0; \
        assertions_failed++; \
    } else { \
        assertions_passed++; \
    } \
} while (0)

#define ASSERT_EQ(a, b, msg) do { \
    if ((a) != (b)) { \
        printf("\n    assertion failed: %s (expected '%d', got '%d') [%s:%d]", \
               msg, (int)(b), (int)(a), __FILE__, __LINE__); \
        _ok = 0; \
        assertions_failed++; \
    } else { \
        assertions_passed++; \
    } \
} while (0)

#define ASSERT_STR_EQ(a, b, msg) do { \
    if (strcmp((a), (b)) != 0) { \
        printf("\n    assertion failed: %s (expected '%s', got '%s') [%s:%d]", \
               msg, (b), (a), __FILE__, __LINE__); \
        _ok = 0; \
        assertions_failed++; \
    } else { \
        assertions_passed++; \
    } \
} while (0)

static int test_summary(void) {
    printf("\n========================================\n");
    printf("  Results: %d passed, %d failed, %d assertions\n",
           tests_passed, tests_failed, assertions_passed + assertions_failed);
    printf("========================================\n");
    return tests_failed > 0;
}

#endif
