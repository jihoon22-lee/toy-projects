#pragma once

// Non-fatal check macros on top of Qt Test. Unlike QVERIFY/COMPARE they keep
// running after a failure, which is what these suites' sequences of
// independent assertions need; every failure still registers as a test fail
// in QTest output.

#include <QTest>
#include <cmath>

#define CHECK(cond)                                                              \
    QTest::qVerify(static_cast<bool>(cond), #cond, "", __FILE__, __LINE__)

#define CHECK_EQ(actual, expected)                                               \
    QTest::qVerify((actual) == (expected), #actual " == " #expected, "",         \
                   __FILE__, __LINE__)

#define CHECK_NEAR(actual, expected, eps)                                        \
    QTest::qVerify(                                                              \
        std::fabs(static_cast<double>(actual) - static_cast<double>(expected))   \
            <= static_cast<double>(eps),                                         \
        #actual " ≈ " #expected " (±" #eps ")", "", __FILE__, __LINE__)
