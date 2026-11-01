/* SPDX-License-Identifier: MPL-2.0 */

#ifndef LKBENCH_H
#define LKBENCH_H

#include <stddef.h>
#include <stdint.h>

#ifndef LKBENCH_VERSION
#define LKBENCH_VERSION "dev"
#endif

struct LkStats {
    double minNs;
    double medianNs;
    double p90Ns;
    double p99Ns;
    double maxNs;
    double meanNs;
    double stddevNs;
    int samples;
};

struct LkOptions {
    uint64_t warmupNs;
    size_t sampleCount;
    unsigned int statsMask;
};

#define LK_STAT_MIN 0x01U
#define LK_STAT_MEDIAN 0x02U
#define LK_STAT_P90 0x04U
#define LK_STAT_P99 0x08U
#define LK_STAT_MAX 0x10U
#define LK_STAT_MEAN 0x20U
#define LK_STAT_STDDEV 0x40U

#define LK_STATS_ALL                                                           \
    (LK_STAT_MIN | LK_STAT_MEDIAN | LK_STAT_P90 | LK_STAT_P99 | LK_STAT_MAX |  \
     LK_STAT_MEAN | LK_STAT_STDDEV)
#define LK_DEFAULT_WARMUP_NS 100000000ULL
#define LK_DEFAULT_SAMPLE_COUNT 100000
#define LK_MAX_SAMPLE_COUNT 10000000

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
int lkTestCaseMatches(const struct LkTestCase *testCase, const char *selector);
int lkRunTestCase(const struct LkTestCase *testCase,
                  const struct LkOptions *options, struct LkStats *stats);

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
