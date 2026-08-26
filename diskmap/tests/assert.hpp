#pragma once

// Minimal hand-rolled test assertion macros (no external framework available).
// Each test .cpp includes this header directly and defines its own main().

#include <cmath>
#include <cstdio>

inline int g_failures = 0; // C++17 inline variable: one definition per binary

#define CHECK(cond)                                                                   \
    do {                                                                              \
        if (!(cond)) {                                                               \
            std::fprintf(stderr, "FAIL %s:%d: CHECK(%s)\n", __FILE__, __LINE__, #cond); \
            ++g_failures;                                                             \
        }                                                                             \
    } while (0)

#define CHECK_EQ(actual, expected)                                                     \
    do {                                                                               \
        if (!((actual) == (expected))) {                                              \
            std::fprintf(stderr, "FAIL %s:%d: CHECK_EQ(%s, %s)\n", __FILE__, __LINE__, \
                         #actual, #expected);                                          \
            ++g_failures;                                                             \
        }                                                                             \
    } while (0)

#define CHECK_NEAR(actual, expected, eps)                                              \
    do {                                                                               \
        const double diskmap_check_near_diff =                                        \
            std::fabs(static_cast<double>(actual) - static_cast<double>(expected));    \
        if (diskmap_check_near_diff > (eps)) {                                        \
            std::fprintf(stderr, "FAIL %s:%d: CHECK_NEAR(%s, %s) diff=%f\n", __FILE__, \
                         __LINE__, #actual, #expected, diskmap_check_near_diff);        \
            ++g_failures;                                                             \
        }                                                                             \
    } while (0)

inline int testSummary() {
    if (g_failures == 0) {
        std::printf("All checks passed\n");
        return 0;
    }
    std::printf("%d check(s) FAILED\n", g_failures);
    return 1;
}
