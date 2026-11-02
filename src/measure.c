/* SPDX-License-Identifier: MPL-2.0 */

#include "lkbench.h"

#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

static int nowNs(uint64_t *ns) {
    struct timespec ts;

    if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0) {
        fprintf(stderr,
                "lkbench: clock_gettime(CLOCK_MONOTONIC) failed (errno %d)\n",
                errno);
        return -1;
    }

    *ns = ((uint64_t)ts.tv_sec * 1000000000ULL) + (uint64_t)ts.tv_nsec;
    return 0;
}

static int warmup(const struct LkTestCase *testCase, uint64_t durationNs) {
    uint64_t start;

    if (durationNs == 0) {
        return 0;
    }
    if (nowNs(&start) != 0) {
        return -1;
    }

    for (;;) {
        uint64_t current;

        testCase->test();
        if (nowNs(&current) != 0) {
            return -1;
        }
        if (current - start >= durationNs) {
            break;
        }
    }

    return 0;
}

#define LK_PROGRESS_INTERVAL_NS 100000000ULL

static int collectSamples(const struct LkTestCase *testCase, uint64_t *values,
                          const struct LkOptions *options) {
    uint64_t nextProgress = 0;

    if (options->progress != NULL) {
        uint64_t start;

        if (nowNs(&start) == 0) {
            nextProgress = start + LK_PROGRESS_INTERVAL_NS;
        }
        options->progress(options->progressContext, testCase->id, 0,
                          options->sampleCount);
    }

    for (size_t i = 0; i < options->sampleCount; i++) {
        uint64_t begin;
        uint64_t end;

        if (nowNs(&begin) != 0) {
            return -1;
        }
        testCase->test();
        if (nowNs(&end) != 0) {
            return -1;
        }
        values[i] = end - begin;

        if (options->progress != NULL && end >= nextProgress) {
            options->progress(options->progressContext, testCase->id, i + 1,
                              options->sampleCount);
            nextProgress = end + LK_PROGRESS_INTERVAL_NS;
        }
    }

    if (options->progress != NULL) {
        options->progress(options->progressContext, testCase->id,
                          options->sampleCount, options->sampleCount);
    }
    return 0;
}

static int compareSamples(const void *leftValue, const void *rightValue) {
    uint64_t left = *(const uint64_t *)leftValue;
    uint64_t right = *(const uint64_t *)rightValue;

    return (left > right) - (left < right);
}

void lkFreeSamples(struct LkSamples *samples) {
    free(samples->values);
    samples->values = NULL;
    samples->count = 0;
    samples->elapsedNs = 0;
}

int lkRunTestCase(const struct LkTestCase *testCase,
                  const struct LkOptions *options, struct LkSamples *samples) {
    uint64_t *values = NULL;
    uint64_t totalNs = 0;
    int result = 0;

    samples->values = NULL;
    samples->count = 0;
    samples->elapsedNs = 0;

    if (testCase->probe != NULL && testCase->probe() != 0) {
        return LK_TEST_SKIPPED;
    }

    if (options->sampleCount == 0 ||
        options->sampleCount > LK_MAX_SAMPLE_COUNT) {
        fprintf(stderr, "lkbench: %s: invalid sample count\n", testCase->id);
        return -1;
    }

    if (testCase->start != NULL && testCase->start() != 0) {
        fprintf(stderr, "lkbench: %s: start failed\n", testCase->id);
        if (testCase->terminate != NULL) {
            testCase->terminate();
        }
        return -1;
    }

    values = malloc(options->sampleCount * sizeof(*values));
    if (values == NULL) {
        fprintf(stderr, "lkbench: %s: out of memory\n", testCase->id);
        result = -1;
        goto done;
    }

    if (warmup(testCase, options->warmupNs) != 0) {
        result = -1;
        goto done;
    }
    if (testCase->reset != NULL) {
        testCase->reset();
    }
    if (collectSamples(testCase, values, options) != 0) {
        result = -1;
        goto done;
    }

    qsort(values, options->sampleCount, sizeof(*values), compareSamples);
    for (size_t i = 0; i < options->sampleCount; i++) {
        totalNs += values[i];
    }

    samples->values = values;
    samples->count = options->sampleCount;
    samples->elapsedNs = totalNs;

done:
    if (testCase->terminate != NULL) {
        testCase->terminate();
    }
    if (result != 0) {
        free(values);
    }
    return result;
}
