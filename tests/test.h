/* test.h — minimal assertion harness. */
#ifndef WAVE_TEST_H
#define WAVE_TEST_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int tests_run = 0;
static int tests_failed = 0;

#define CHECK(cond)                                                       \
    do {                                                                  \
        tests_run++;                                                      \
        if (!(cond)) {                                                    \
            tests_failed++;                                               \
            fprintf(stderr, "  FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
        }                                                                 \
    } while (0)

#define CHECK_STR(a, b)                                                   \
    do {                                                                  \
        tests_run++;                                                      \
        if (strcmp((a), (b)) != 0) {                                      \
            tests_failed++;                                               \
            fprintf(stderr, "  FAIL %s:%d: \"%s\" != \"%s\"\n",           \
                    __FILE__, __LINE__, (a), (b));                        \
        }                                                                 \
    } while (0)

#define CHECK_EQ(a, b)                                                    \
    do {                                                                  \
        tests_run++;                                                      \
        long _a = (long)(a), _b = (long)(b);                             \
        if (_a != _b) {                                                   \
            tests_failed++;                                               \
            fprintf(stderr, "  FAIL %s:%d: %ld != %ld\n",                 \
                    __FILE__, __LINE__, _a, _b);                          \
        }                                                                 \
    } while (0)

#define TEST_REPORT()                                                     \
    do {                                                                  \
        printf("   %d checks, %d failed\n", tests_run, tests_failed);     \
        return tests_failed ? 1 : 0;                                      \
    } while (0)

#endif /* WAVE_TEST_H */
