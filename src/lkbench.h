/* SPDX-License-Identifier: MPL-2.0 */

#ifndef LKBENCH_H
#define LKBENCH_H

#include <stddef.h>

struct LkStats {
    double minNs;
    double medianNs;
    int samples;
};

struct LkTestCase {
    char id[256];
    const char *file;
    int (*start)(void);
    void (*test)(void);
    void (*terminate)(void);
};

void lkRegisterTestCase(struct LkTestCase *testCase);
size_t lkTestCaseCount(void);
struct LkTestCase *lkTestCaseAt(size_t index);
int lkRunTestCase(const struct LkTestCase *testCase, struct LkStats *stats);

#ifdef __has_attribute
#if __has_attribute(constructor)
#define LK_CONSTRUCTOR __attribute__((constructor))
#endif
#endif

#ifndef LK_CONSTRUCTOR
#if defined(__GNUC__) || defined(__clang__)
#define LK_CONSTRUCTOR __attribute__((constructor))
#else
#error                                                                         \
    "lkbench requires GCC/Clang constructor attribute for test auto-registration"
#endif
#endif

#define LK_TEST()                                                              \
    static struct LkTestCase lkTestCase = {                                    \
        .file = __FILE__,                                                      \
        .start = start,                                                        \
        .test = test,                                                          \
        .terminate = terminate,                                                \
    };                                                                         \
    static void LK_CONSTRUCTOR lkTestAutoRegister(void) {                      \
        lkRegisterTestCase(&lkTestCase);                                       \
    }

#endif
