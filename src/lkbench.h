/* SPDX-License-Identifier: MPL-2.0 */

#ifndef LKBENCH_H
#define LKBENCH_H

#include <stddef.h>
#include <stdint.h>

#ifndef LKBENCH_VERSION
#define LKBENCH_VERSION "dev"
#endif

struct LkSamples {
    uint64_t *values;
    size_t count;
    uint64_t elapsedNs;
};

struct LkMetric {
    const char *name;
    const char *unit;
    double (*compute)(const struct LkSamples *samples);
};

typedef void (*LkProgressFn)(void *context, const char *testId,
                             size_t completed, size_t total);

struct LkOptions {
    uint64_t warmupNs;
    size_t sampleCount;
    LkProgressFn progress;
    void *progressContext;
};

#define LK_DEFAULT_WARMUP_NS 100000000ULL
#define LK_DEFAULT_SAMPLE_COUNT 100000
#define LK_MAX_SAMPLE_COUNT 10000000

struct LkTestCase {
    char id[256];
    const char *file;
    int (*start)(void);
    void (*test)(void);
    void (*terminate)(void);
    void (*reset)(void);
    const struct LkMetric *metrics;
    size_t metricCount;
    int (*probe)(void);
};

#define LK_TEST_SKIPPED 2

void lkRegisterTestCase(struct LkTestCase *testCase);
void lkRegisterMetrics(const char *file, const struct LkMetric *metrics,
                       size_t count);
size_t lkTestCaseCount(void);
struct LkTestCase *lkTestCaseAt(size_t index);
int lkTestCaseMatches(const struct LkTestCase *testCase, const char *selector);
int lkRunTestCase(const struct LkTestCase *testCase,
                  const struct LkOptions *options, struct LkSamples *samples);
void lkFreeSamples(struct LkSamples *samples);

double lkMetricLatencyMin(const struct LkSamples *samples);
double lkMetricLatencyMedian(const struct LkSamples *samples);
double lkMetricLatencyP90(const struct LkSamples *samples);
double lkMetricLatencyP99(const struct LkSamples *samples);
double lkMetricLatencyMax(const struct LkSamples *samples);
double lkMetricLatencyMean(const struct LkSamples *samples);
double lkMetricLatencyStddev(const struct LkSamples *samples);

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

#define LK_METRIC(metricName, metricUnit, metricCompute)                       \
    {(metricName), (metricUnit), (metricCompute)}

#define LK_METRICS_LATENCY_LIST                                                \
    LK_METRIC("min", "ns", lkMetricLatencyMin),                                \
        LK_METRIC("median", "ns", lkMetricLatencyMedian),                      \
        LK_METRIC("p90", "ns", lkMetricLatencyP90),                            \
        LK_METRIC("p99", "ns", lkMetricLatencyP99),                            \
        LK_METRIC("max", "ns", lkMetricLatencyMax),                            \
        LK_METRIC("mean", "ns", lkMetricLatencyMean),                          \
        LK_METRIC("stddev", "ns", lkMetricLatencyStddev)

#define LK_METRICS(...)                                                        \
    static const struct LkMetric lkMetrics[] = {__VA_ARGS__};                  \
    static void LK_CONSTRUCTOR lkMetricsAutoRegister(void) {                   \
        lkRegisterMetrics(__FILE__, lkMetrics,                                 \
                          sizeof(lkMetrics) / sizeof(lkMetrics[0]));           \
    }

#define LK_METRICS_LATENCY() LK_METRICS(LK_METRICS_LATENCY_LIST)

#define LK_TEST_WITH_RESET(resetFn)                                            \
    static struct LkTestCase lkTestCase = {                                    \
        .file = __FILE__,                                                      \
        .start = start,                                                        \
        .test = test,                                                          \
        .terminate = terminate,                                                \
        .reset = (resetFn),                                                    \
    };                                                                         \
    static void LK_CONSTRUCTOR lkTestAutoRegister(void) {                      \
        lkRegisterTestCase(&lkTestCase);                                       \
    }

#define LK_TEST_OPTIONAL(probeFn)                                              \
    static struct LkTestCase lkTestCase = {                                    \
        .file = __FILE__,                                                      \
        .start = start,                                                        \
        .test = test,                                                          \
        .terminate = terminate,                                                \
        .probe = (probeFn),                                                    \
    };                                                                         \
    static void LK_CONSTRUCTOR lkTestAutoRegister(void) {                      \
        lkRegisterTestCase(&lkTestCase);                                       \
    }

#define LK_TEST() LK_TEST_WITH_RESET(NULL)

#endif
