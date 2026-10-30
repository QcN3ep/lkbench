/* SPDX-License-Identifier: MPL-2.0 */

#include "lkbench.h"

#include <getopt.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAX_SELECTORS 64

static void printUsage(FILE *stream, const char *program) {
    fprintf(stream, "Usage: %s [OPTIONS]\n", program);
    fprintf(stream, "\n");
    fprintf(stream, "Options:\n");
    fprintf(stream, "  -h, --help             Show this help\n");
    fprintf(stream, "  -v, --version          Show version\n");
    fprintf(stream, "  -l, --list             List all test IDs\n");
    fprintf(
        stream,
        "  -b, --bench <selector> Run tests matching selector (repeatable)\n");
}

static void printVersion(void) { printf("lkbench %s\n", LKBENCH_VERSION); }

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
    static const struct option options[] = {
        {"help", no_argument, NULL, 'h'},
        {"version", no_argument, NULL, 'v'},
        {"list", no_argument, NULL, 'l'},
        {"bench", required_argument, NULL, 'b'},
        {NULL, 0, NULL, 0},
    };
    const char *program = argc > 0 ? argv[0] : "lkbench";
    const char *selectors[MAX_SELECTORS];
    size_t selectorCount = 0;
    int listOnly = 0;
    int option;
    size_t count;
    size_t idWidth;
    unsigned char *selected = NULL;
    int failures = 0;

    opterr = 0;
    while ((option = getopt_long(argc, argv, ":hlvb:", options, NULL)) != -1) {
        switch (option) {
        case 'h':
            printUsage(stdout, program);
            return 0;
        case 'v':
            printVersion();
            return 0;
        case 'l':
            listOnly = 1;
            break;
        case 'b':
            if (selectorCount >= MAX_SELECTORS) {
                fprintf(stderr, "%s: too many --bench selectors\n", program);
                return 2;
            }
            selectors[selectorCount] = optarg;
            selectorCount++;
            break;
        case ':':
            fprintf(stderr, "%s: option '-%c' requires an argument\n", program,
                    optopt);
            printUsage(stderr, program);
            return 2;
        case '?':
        default:
            if (optopt != 0) {
                fprintf(stderr, "%s: unknown option '-%c'\n", program, optopt);
            } else {
                fprintf(stderr, "%s: unknown option '%s'\n", program,
                        argv[optind - 1]);
            }
            printUsage(stderr, program);
            return 2;
        }
    }

    if (optind < argc) {
        fprintf(stderr, "%s: unexpected argument '%s'\n", program,
                argv[optind]);
        printUsage(stderr, program);
        return 2;
    }

    count = lkTestCaseCount();
    if (listOnly != 0) {
        for (size_t i = 0; i < count; i++) {
            printf("%s\n", lkTestCaseAt(i)->id);
        }
        return 0;
    }

    if (selectorCount > 0) {
        selected = calloc(count == 0 ? 1 : count, sizeof(*selected));
        if (selected == NULL) {
            fprintf(stderr, "%s: out of memory\n", program);
            return 1;
        }

        for (size_t i = 0; i < selectorCount; i++) {
            size_t matches = 0;

            for (size_t j = 0; j < count; j++) {
                if (lkTestCaseMatches(lkTestCaseAt(j), selectors[i]) != 0) {
                    selected[j] = 1;
                    matches++;
                }
            }
            if (matches == 0) {
                fprintf(stderr, "%s: no tests match '%s'\n", program,
                        selectors[i]);
                free(selected);
                return 2;
            }
        }
    }

    idWidth = maxIdWidth();
    for (size_t i = 0; i < count; i++) {
        struct LkTestCase *testCase = lkTestCaseAt(i);
        struct LkStats stats;

        if (selected != NULL && selected[i] == 0) {
            continue;
        }
        if (lkRunTestCase(testCase, &stats) != 0) {
            failures++;
            continue;
        }

        printf("%-*s  min %9.1f ns  median %9.1f ns  (%d samples)\n",
               (int)idWidth, testCase->id, stats.minNs, stats.medianNs,
               stats.samples);
    }

    free(selected);
    return failures == 0 ? 0 : 1;
}
