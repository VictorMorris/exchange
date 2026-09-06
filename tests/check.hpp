#pragma once
#include <cstdio>

// Minimal harness: a test is a void() that CHECKs. No framework, no dependency.
// A test that fires no CHECK reports "todo", not "ok" — an empty test is not a
// passing test, and this suite is mostly empty by design.
namespace test {
inline int failures = 0;
inline int checks   = 0;
inline int empty    = 0;

// Exit code, so ctest agrees with what the output says. An empty test is not a
// passing test: a suite that fired no CHECK must not report success just because
// it also reported no failure.
inline int summary() {
    std::printf("\n%d checks, %d failures, %d empty\n", checks, failures, empty);
    return (failures != 0 || empty != 0) ? 1 : 0;
}
}

#define CHECK(cond)                                                        \
    do {                                                                   \
        ++test::checks;                                                    \
        if (!(cond)) {                                                     \
            ++test::failures;                                              \
            std::printf("  FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond);  \
        }                                                                  \
    } while (0)

#define RUN(fn)                                                            \
    do {                                                                   \
        const int f0 = test::failures, c0 = test::checks;                  \
        fn();                                                              \
        if (test::checks == c0) {                                          \
            ++test::empty;                                                 \
            std::printf("todo %s\n", #fn);                                 \
        } else if (test::failures == f0) {                                 \
            std::printf("ok   %s\n", #fn);                                 \
        } else {                                                           \
            std::printf("FAIL %s\n", #fn);                                 \
        }                                                                  \
    } while (0)
