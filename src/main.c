/* SPDX-License-Identifier: MPL-2.0 */

#include "lkbench.h"

#include <errno.h>
#include <getopt.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <yyjson.h>

#define MAX_SELECTORS 64

enum LkFormat {
    LkFormatText,
    LkFormatJson,
};

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
    fprintf(stream,
            "  -f, --format <format>  Output format: text (default), json\n");
    fprintf(stream, "  -w, --warmup <duration>  Warmup time (default 100ms)\n");
    fprintf(stream,
            "  -n, --samples <count>    Samples per test (default 100000)\n");
    fprintf(stream, "  -s, --stats <list>       Stats: "
                    "min,median,p90,p99,max,mean,stddev\n");
}

static void printVersion(void) { printf("lkbench %s\n", LKBENCH_VERSION); }

static int parseFormat(const char *value, enum LkFormat *format) {
    if (strcmp(value, "text") == 0) {
        *format = LkFormatText;
        return 0;
    }
    if (strcmp(value, "json") == 0) {
        *format = LkFormatJson;
        return 0;
    }
    return -1;
}

static int parseDuration(const char *value, uint64_t *nanoseconds) {
    char *end = NULL;
    unsigned long long number;
    uint64_t multiplier;

    if (value[0] == '\0' || value[0] == '-') {
        return -1;
    }

    errno = 0;
    number = strtoull(value, &end, 10);
    if (errno != 0 || end == value) {
        return -1;
    }

    if (*end == '\0' || strcmp(end, "ms") == 0) {
        multiplier = 1000000ULL;
    } else if (strcmp(end, "us") == 0) {
        multiplier = 1000ULL;
    } else if (strcmp(end, "s") == 0) {
        multiplier = 1000000000ULL;
    } else {
        return -1;
    }

    if (number > UINT64_MAX / multiplier) {
        return -1;
    }
    *nanoseconds = (uint64_t)number * multiplier;
    return 0;
}

static int parseSampleCount(const char *value, size_t *count) {
    char *end = NULL;
    unsigned long long number;

    if (value[0] == '\0' || value[0] == '-') {
        return -1;
    }

    errno = 0;
    number = strtoull(value, &end, 10);
    if (errno != 0 || end == value || *end != '\0') {
        return -1;
    }
    if (number == 0 || number > LK_MAX_SAMPLE_COUNT) {
        return -1;
    }

    *count = (size_t)number;
    return 0;
}

static unsigned int statFlagFromName(const char *name, size_t length) {
    if (length == 3 && strncmp(name, "min", length) == 0) {
        return LK_STAT_MIN;
    }
    if (length == 6 && strncmp(name, "median", length) == 0) {
        return LK_STAT_MEDIAN;
    }
    if (length == 3 && strncmp(name, "p50", length) == 0) {
        return LK_STAT_MEDIAN;
    }
    if (length == 3 && strncmp(name, "p90", length) == 0) {
        return LK_STAT_P90;
    }
    if (length == 3 && strncmp(name, "p99", length) == 0) {
        return LK_STAT_P99;
    }
    if (length == 3 && strncmp(name, "max", length) == 0) {
        return LK_STAT_MAX;
    }
    if (length == 4 && strncmp(name, "mean", length) == 0) {
        return LK_STAT_MEAN;
    }
    if (length == 6 && strncmp(name, "stddev", length) == 0) {
        return LK_STAT_STDDEV;
    }
    return 0;
}

static int parseStatsMask(const char *value, unsigned int *mask) {
    const char *cursor = value;
    unsigned int result = 0;

    if (*cursor == '\0') {
        return -1;
    }

    for (;;) {
        const char *comma = strchr(cursor, ',');
        size_t length =
            comma != NULL ? (size_t)(comma - cursor) : strlen(cursor);
        unsigned int flag = statFlagFromName(cursor, length);

        if (flag == 0) {
            return -1;
        }
        result |= flag;

        if (comma == NULL) {
            break;
        }
        cursor = comma + 1;
        if (*cursor == '\0') {
            return -1;
        }
    }

    *mask = result;
    return 0;
}

static int appendJsonStats(yyjson_mut_doc *doc, yyjson_mut_val *item,
                           const struct LkStats *stats, unsigned int mask) {
    if ((mask & LK_STAT_MIN) != 0 &&
        !yyjson_mut_obj_add_real(doc, item, "minNs", stats->minNs)) {
        return -1;
    }
    if ((mask & LK_STAT_MEDIAN) != 0 &&
        !yyjson_mut_obj_add_real(doc, item, "medianNs", stats->medianNs)) {
        return -1;
    }
    if ((mask & LK_STAT_P90) != 0 &&
        !yyjson_mut_obj_add_real(doc, item, "p90Ns", stats->p90Ns)) {
        return -1;
    }
    if ((mask & LK_STAT_P99) != 0 &&
        !yyjson_mut_obj_add_real(doc, item, "p99Ns", stats->p99Ns)) {
        return -1;
    }
    if ((mask & LK_STAT_MAX) != 0 &&
        !yyjson_mut_obj_add_real(doc, item, "maxNs", stats->maxNs)) {
        return -1;
    }
    if ((mask & LK_STAT_MEAN) != 0 &&
        !yyjson_mut_obj_add_real(doc, item, "meanNs", stats->meanNs)) {
        return -1;
    }
    if ((mask & LK_STAT_STDDEV) != 0 &&
        !yyjson_mut_obj_add_real(doc, item, "stddevNs", stats->stddevNs)) {
        return -1;
    }
    if (!yyjson_mut_obj_add_int(doc, item, "samples", stats->samples)) {
        return -1;
    }
    return 0;
}

static int appendJsonResult(yyjson_mut_doc *doc, yyjson_mut_val *results,
                            const struct LkTestCase *testCase,
                            const struct LkStats *stats, unsigned int mask) {
    yyjson_mut_val *item = yyjson_mut_obj(doc);

    if (item == NULL) {
        return -1;
    }
    if (!yyjson_mut_obj_add_str(doc, item, "id", testCase->id) ||
        appendJsonStats(doc, item, stats, mask) != 0 ||
        !yyjson_mut_arr_append(results, item)) {
        return -1;
    }
    return 0;
}

static void printTextResult(const struct LkTestCase *testCase,
                            const struct LkStats *stats, unsigned int mask,
                            size_t idWidth) {
    printf("%-*s", (int)idWidth, testCase->id);
    if ((mask & LK_STAT_MIN) != 0) {
        printf("  min %9.1f ns", stats->minNs);
    }
    if ((mask & LK_STAT_MEDIAN) != 0) {
        printf("  median %9.1f ns", stats->medianNs);
    }
    if ((mask & LK_STAT_P90) != 0) {
        printf("  p90 %9.1f ns", stats->p90Ns);
    }
    if ((mask & LK_STAT_P99) != 0) {
        printf("  p99 %9.1f ns", stats->p99Ns);
    }
    if ((mask & LK_STAT_MAX) != 0) {
        printf("  max %9.1f ns", stats->maxNs);
    }
    if ((mask & LK_STAT_MEAN) != 0) {
        printf("  mean %9.1f ns", stats->meanNs);
    }
    if ((mask & LK_STAT_STDDEV) != 0) {
        printf("  stddev %9.1f ns", stats->stddevNs);
    }
    printf("  (samples %d)\n", stats->samples);
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
    static const struct option options[] = {
        {"help", no_argument, NULL, 'h'},
        {"version", no_argument, NULL, 'v'},
        {"list", no_argument, NULL, 'l'},
        {"bench", required_argument, NULL, 'b'},
        {"format", required_argument, NULL, 'f'},
        {"warmup", required_argument, NULL, 'w'},
        {"samples", required_argument, NULL, 'n'},
        {"stats", required_argument, NULL, 's'},
        {NULL, 0, NULL, 0},
    };
    const char *program = argc > 0 ? argv[0] : "lkbench";
    const char *selectors[MAX_SELECTORS];
    const char *formatValue = NULL;
    size_t selectorCount = 0;
    int listOnly = 0;
    int option;
    size_t count;
    size_t idWidth;
    unsigned char *selected = NULL;
    enum LkFormat format = LkFormatText;
    struct LkOptions runOptions = {
        LK_DEFAULT_WARMUP_NS,
        LK_DEFAULT_SAMPLE_COUNT,
        LK_STATS_ALL,
    };
    yyjson_mut_doc *jsonDoc = NULL;
    yyjson_mut_val *jsonResults = NULL;
    int failures = 0;

    opterr = 0;
    while ((option = getopt_long(argc, argv, ":hlvb:f:w:n:s:", options,
                                 NULL)) != -1) {
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
        case 'f':
            formatValue = optarg;
            break;
        case 'w':
            if (parseDuration(optarg, &runOptions.warmupNs) != 0) {
                fprintf(stderr, "%s: invalid warmup '%s'\n", program, optarg);
                printUsage(stderr, program);
                return 2;
            }
            break;
        case 'n':
            if (parseSampleCount(optarg, &runOptions.sampleCount) != 0) {
                fprintf(stderr, "%s: invalid sample count '%s'\n", program,
                        optarg);
                printUsage(stderr, program);
                return 2;
            }
            break;
        case 's':
            if (parseStatsMask(optarg, &runOptions.statsMask) != 0) {
                fprintf(stderr, "%s: invalid stats list '%s'\n", program,
                        optarg);
                printUsage(stderr, program);
                return 2;
            }
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

    if (formatValue != NULL && parseFormat(formatValue, &format) != 0) {
        fprintf(stderr, "%s: unknown format '%s'\n", program, formatValue);
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

    if (format == LkFormatJson) {
        jsonDoc = yyjson_mut_doc_new(NULL);
        if (jsonDoc == NULL) {
            fprintf(stderr, "%s: out of memory\n", program);
            free(selected);
            return 1;
        }
        jsonResults = yyjson_mut_arr(jsonDoc);
        if (jsonResults == NULL) {
            fprintf(stderr, "%s: out of memory\n", program);
            free(selected);
            yyjson_mut_doc_free(jsonDoc);
            return 1;
        }
        yyjson_mut_doc_set_root(jsonDoc, jsonResults);
    }

    idWidth = maxIdWidth();
    for (size_t i = 0; i < count; i++) {
        struct LkTestCase *testCase = lkTestCaseAt(i);
        struct LkStats stats;

        if (selected != NULL && selected[i] == 0) {
            continue;
        }
        if (lkRunTestCase(testCase, &runOptions, &stats) != 0) {
            failures++;
            continue;
        }

        if (format == LkFormatJson) {
            if (appendJsonResult(jsonDoc, jsonResults, testCase, &stats,
                                 runOptions.statsMask) != 0) {
                fprintf(stderr, "%s: failed to build JSON output\n", program);
                free(selected);
                yyjson_mut_doc_free(jsonDoc);
                return 1;
            }
        } else {
            printTextResult(testCase, &stats, runOptions.statsMask, idWidth);
        }
    }

    if (format == LkFormatJson) {
        char *json = yyjson_mut_write(jsonDoc, YYJSON_WRITE_PRETTY, NULL);

        if (json == NULL) {
            fprintf(stderr, "%s: failed to serialize JSON output\n", program);
            free(selected);
            yyjson_mut_doc_free(jsonDoc);
            return 1;
        }
        puts(json);
        free(json);
        yyjson_mut_doc_free(jsonDoc);
    }

    free(selected);
    return failures == 0 ? 0 : 1;
}
