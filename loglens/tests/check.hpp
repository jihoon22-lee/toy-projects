#pragma once

// Minimal hand-rolled assertions. ici compiles each tests/*.cpp into its own
// binary with no framework linked, so every test file defines its own main()
// and returns checkSummary().

#include <cstdio>

inline int g_checkFailures = 0;

#define CHECK(cond)                                                                     \
    do {                                                                                \
        if (!(cond)) {                                                                  \
            std::fprintf(stderr, "FAIL %s:%d: CHECK(%s)\n", __FILE__, __LINE__, #cond);  \
            ++g_checkFailures;                                                          \
        }                                                                               \
    } while (0)

#define CHECK_EQ(actual, expected)                                                      \
    do {                                                                                \
        if (!((actual) == (expected))) {                                                \
            std::fprintf(stderr, "FAIL %s:%d: CHECK_EQ(%s, %s)\n", __FILE__, __LINE__,   \
                         #actual, #expected);                                            \
            ++g_checkFailures;                                                          \
        }                                                                               \
    } while (0)

inline int checkSummary() {
    if (g_checkFailures == 0) {
        std::printf("All checks passed\n");
        return 0;
    }
    std::printf("%d check(s) FAILED\n", g_checkFailures);
    return 1;
}
