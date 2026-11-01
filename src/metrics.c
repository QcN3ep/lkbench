/* SPDX-License-Identifier: MPL-2.0 */

#include "lkbench.h"

#include <math.h>

static double percentile(const struct LkSamples *samples,
                         unsigned int percent) {
    size_t rank = ((samples->count * percent) + 99) / 100;

    if (rank == 0) {
        rank = 1;
    }
    if (rank > samples->count) {
        rank = samples->count;
    }
    return (double)samples->values[rank - 1];
}

double lkMetricLatencyMin(const struct LkSamples *samples) {
    return samples->count == 0 ? 0.0 : (double)samples->values[0];
}

double lkMetricLatencyMedian(const struct LkSamples *samples) {
    return samples->count == 0 ? 0.0 : percentile(samples, 50);
}

double lkMetricLatencyP90(const struct LkSamples *samples) {
    return samples->count == 0 ? 0.0 : percentile(samples, 90);
}

double lkMetricLatencyP99(const struct LkSamples *samples) {
    return samples->count == 0 ? 0.0 : percentile(samples, 99);
}

double lkMetricLatencyMax(const struct LkSamples *samples) {
    return samples->count == 0 ? 0.0
                               : (double)samples->values[samples->count - 1];
}

double lkMetricLatencyMean(const struct LkSamples *samples) {
    double sum = 0.0;

    if (samples->count == 0) {
        return 0.0;
    }
    for (size_t i = 0; i < samples->count; i++) {
        sum += (double)samples->values[i];
    }
    return sum / (double)samples->count;
}

double lkMetricLatencyStddev(const struct LkSamples *samples) {
    double mean;
    double variance = 0.0;

    if (samples->count < 2) {
        return 0.0;
    }
    mean = lkMetricLatencyMean(samples);
    for (size_t i = 0; i < samples->count; i++) {
        double diff = (double)samples->values[i] - mean;
        variance += diff * diff;
    }
    return sqrt(variance / (double)(samples->count - 1));
}
