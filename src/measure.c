/* SPDX-License-Identifier: MPL-2.0 */

#include "lkbench.h"

#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <time.h>

#define BATCH_TARGET_NS (50LL * 1000 * 1000)
#define MAX_BATCH_ITERS 16777216UL
#define WARMUP_TARGET_NS (100LL * 1000 * 1000)
#define SAMPLE_COUNT 5

static int nowNs(int64_t *ns) {
    struct timespec ts;

    if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0) {
        fprintf(stderr,
                "lkbench: clock_gettime(CLOCK_MONOTONIC) failed (errno %d)\n",
                errno);
        return -1;
    }

    *ns = ((int64_t)ts.tv_sec * 1000000000LL) + (int64_t)ts.tv_nsec;
    return 0;
}

static int measureBatch(const struct LkTestCase *testCase,
                        unsigned long iterations, int64_t *elapsedNs) {
    int64_t start;
    int64_t end;

    if (nowNs(&start) != 0) {
        return -1;
    }
    for (unsigned long i = 0; i < iterations; i++) {
        testCase->test();
    }
    if (nowNs(&end) != 0) {
        return -1;
    }

    *elapsedNs = end - start;
    return 0;
}

static int calibrate(const struct LkTestCase *testCase,
                     unsigned long *iterations) {
    unsigned long candidates = 1000;
    int64_t elapsedNs = 0;

    for (;;) {
        if (measureBatch(testCase, candidates, &elapsedNs) != 0) {
            return -1;
        }
        if (elapsedNs >= BATCH_TARGET_NS || candidates >= MAX_BATCH_ITERS) {
            break;
        }
        candidates *= 2;
    }

    *iterations = candidates;
    return 0;
}

static int warmup(const struct LkTestCase *testCase, unsigned long iterations) {
    int64_t totalNs = 0;

    while (totalNs < WARMUP_TARGET_NS) {
        int64_t elapsedNs = 0;

        if (measureBatch(testCase, iterations, &elapsedNs) != 0) {
            return -1;
        }
        totalNs += elapsedNs;
    }

    return 0;
}

static void sortValues(double *values, int count) {
    for (int i = 1; i < count; i++) {
        double value = values[i];
        int j = i - 1;

        while (j >= 0 && values[j] > value) {
            values[j + 1] = values[j];
            j--;
        }
        values[j + 1] = value;
    }
}

static int sampleLatency(const struct LkTestCase *testCase,
                         unsigned long iterations, struct LkStats *stats) {
    double values[SAMPLE_COUNT];

    for (int i = 0; i < SAMPLE_COUNT; i++) {
        int64_t elapsedNs = 0;

        if (measureBatch(testCase, iterations, &elapsedNs) != 0) {
            return -1;
        }
        values[i] = (double)elapsedNs / (double)iterations;
    }

    sortValues(values, SAMPLE_COUNT);
    stats->minNs = values[0];
    stats->medianNs = values[SAMPLE_COUNT / 2];
    stats->samples = SAMPLE_COUNT;
    return 0;
}

int lkRunTestCase(const struct LkTestCase *testCase, struct LkStats *stats) {
    unsigned long iterations = 0;
    int result = 0;

    if (testCase->start != NULL && testCase->start() != 0) {
        fprintf(stderr, "lkbench: %s: start failed\n", testCase->id);
        return -1;
    }

    if (calibrate(testCase, &iterations) != 0 ||
        warmup(testCase, iterations) != 0 ||
        sampleLatency(testCase, iterations, stats) != 0) {
        result = -1;
    }

    if (testCase->terminate != NULL) {
        testCase->terminate();
    }

    return result;
}
