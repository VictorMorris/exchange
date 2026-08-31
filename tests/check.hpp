#pragma once
#include <cstdio>

// Minimal harness: a test is a void() that CHECKs
namespace test {
inline int failures = 0;
inline int checks   = 0;
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
        const int before = test::failures;                                 \
        fn();                                                              \
        std::printf("%s %s\n", test::failures == before ? "ok  " : "FAIL", #fn); \
    } while (0)
