/* SPDX-License-Identifier: MPL-2.0 */

#include "lkbench.h"

#include <errno.h>
#include <math.h>
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

static int collectSamples(const struct LkTestCase *testCase, uint64_t *samples,
                          size_t count) {
    for (size_t i = 0; i < count; i++) {
        uint64_t begin;
        uint64_t end;

        if (nowNs(&begin) != 0) {
            return -1;
        }
        testCase->test();
        if (nowNs(&end) != 0) {
            return -1;
        }
        samples[i] = end - begin;
    }

    return 0;
}

static int compareSamples(const void *leftValue, const void *rightValue) {
    uint64_t left = *(const uint64_t *)leftValue;
    uint64_t right = *(const uint64_t *)rightValue;

    return (left > right) - (left < right);
}

static double percentile(const uint64_t *sorted, size_t count,
                         unsigned int percent) {
    size_t rank = ((count * percent) + 99) / 100;

    if (rank == 0) {
        rank = 1;
    }
    if (rank > count) {
        rank = count;
    }
    return (double)sorted[rank - 1];
}

static void computeStats(uint64_t *samples, size_t count,
                         struct LkStats *stats) {
    double sum = 0.0;

    qsort(samples, count, sizeof(*samples), compareSamples);

    stats->minNs = (double)samples[0];
    stats->medianNs = percentile(samples, count, 50);
    stats->p90Ns = percentile(samples, count, 90);
    stats->p99Ns = percentile(samples, count, 99);
    stats->maxNs = (double)samples[count - 1];

    for (size_t i = 0; i < count; i++) {
        sum += (double)samples[i];
    }
    stats->meanNs = sum / (double)count;

    if (count > 1) {
        double variance = 0.0;

        for (size_t i = 0; i < count; i++) {
            double diff = (double)samples[i] - stats->meanNs;
            variance += diff * diff;
        }
        stats->stddevNs = sqrt(variance / (double)(count - 1));
    } else {
        stats->stddevNs = 0.0;
    }

    stats->samples = (int)count;
}

int lkRunTestCase(const struct LkTestCase *testCase,
                  const struct LkOptions *options, struct LkStats *stats) {
    uint64_t *samples = NULL;
    int result = 0;

    if (options->sampleCount == 0 ||
        options->sampleCount > LK_MAX_SAMPLE_COUNT) {
        fprintf(stderr, "lkbench: %s: invalid sample count\n", testCase->id);
        return -1;
    }

    if (testCase->start != NULL && testCase->start() != 0) {
        fprintf(stderr, "lkbench: %s: start failed\n", testCase->id);
        return -1;
    }

    samples = malloc(options->sampleCount * sizeof(*samples));
    if (samples == NULL) {
        fprintf(stderr, "lkbench: %s: out of memory\n", testCase->id);
        result = -1;
        goto done;
    }

    if (warmup(testCase, options->warmupNs) != 0 ||
        collectSamples(testCase, samples, options->sampleCount) != 0) {
        result = -1;
        goto done;
    }

    computeStats(samples, options->sampleCount, stats);

done:
    if (testCase->terminate != NULL) {
        testCase->terminate();
    }
    free(samples);
    return result;
}
