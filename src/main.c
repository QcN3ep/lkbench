/* SPDX-License-Identifier: MPL-2.0 */

#include "lkbench.h"

#include <stdio.h>
#include <string.h>

static void printUsage(FILE *stream, const char *program) {
    fprintf(stream, "Usage: %s [--help]\n", program);
    fprintf(stream, "Run all built-in benchmarks (default).\n");
}

static size_t maxIdWidth(void) {
    size_t count = lkTestCaseCount();
    size_t width = 0;

    for (size_t i = 0; i < count; i++) {
        struct LkTestCase *testCase = lkTestCaseAt(i);
        size_t length = strlen(testCase->id);

        if (length > width) {
            width = length;
        }
    }

    return width;
}

int main(int argc, char **argv) {
    const char *program = argc > 0 ? argv[0] : "lkbench";
    size_t count;
    size_t idWidth;
    int failures = 0;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--help") == 0) {
            printUsage(stdout, program);
            return 0;
        }

        fprintf(stderr, "lkbench: unknown argument: %s\n", argv[i]);
        printUsage(stderr, program);
        return 2;
    }

    count = lkTestCaseCount();
    idWidth = maxIdWidth();
    for (size_t i = 0; i < count; i++) {
        struct LkTestCase *testCase = lkTestCaseAt(i);
        struct LkStats stats;

        if (lkRunTestCase(testCase, &stats) != 0) {
            failures++;
            continue;
        }

        printf("%-*s  min %9.1f ns  median %9.1f ns  (%d samples)\n",
               (int)idWidth, testCase->id, stats.minNs, stats.medianNs,
               stats.samples);
    }

    return failures == 0 ? 0 : 1;
}
