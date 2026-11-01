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
    fprintf(stream,
            "  -s, --stats <list>       Metrics to output (comma separated)\n");
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

static int validateMetricFilter(const char *value) {
    const char *cursor = value;

    if (*cursor == '\0') {
        return -1;
    }

    for (;;) {
        const char *comma = strchr(cursor, ',');
        size_t length =
            comma != NULL ? (size_t)(comma - cursor) : strlen(cursor);

        if (length == 0) {
            return -1;
        }
        if (comma == NULL) {
            break;
        }
        cursor = comma + 1;
        if (*cursor == '\0') {
            return -1;
        }
    }

    return 0;
}

static int metricNameMatches(const char *name, const char *token,
                             size_t length) {
    if (strlen(name) == length && strncmp(name, token, length) == 0) {
        return 1;
    }
    return length == 3 && strncmp(token, "p50", 3) == 0 &&
           strcmp(name, "median") == 0;
}

static int metricSelected(const char *name, const char *filter) {
    const char *cursor = filter;

    if (filter == NULL) {
        return 1;
    }

    for (;;) {
        const char *comma = strchr(cursor, ',');
        size_t length =
            comma != NULL ? (size_t)(comma - cursor) : strlen(cursor);

        if (metricNameMatches(name, cursor, length)) {
            return 1;
        }
        if (comma == NULL) {
            break;
        }
        cursor = comma + 1;
    }

    return 0;
}

static int metricFilterKnown(const char *filter, size_t testCount) {
    const char *cursor = filter;

    for (;;) {
        const char *comma = strchr(cursor, ',');
        size_t length =
            comma != NULL ? (size_t)(comma - cursor) : strlen(cursor);
        int found = 0;

        for (size_t i = 0; i < testCount && found == 0; i++) {
            struct LkTestCase *testCase = lkTestCaseAt(i);

            for (size_t j = 0; j < testCase->metricCount; j++) {
                if (metricNameMatches(testCase->metrics[j].name, cursor,
                                      length)) {
                    found = 1;
                    break;
                }
            }
        }

        if (found == 0) {
            return 0;
        }
        if (comma == NULL) {
            break;
        }
        cursor = comma + 1;
    }

    return 1;
}

static int appendJsonMetrics(yyjson_mut_doc *doc, yyjson_mut_val *item,
                             const struct LkTestCase *testCase,
                             const struct LkSamples *samples,
                             const char *filter) {
    yyjson_mut_val *metrics = yyjson_mut_arr(doc);

    if (metrics == NULL) {
        return -1;
    }

    for (size_t i = 0; i < testCase->metricCount; i++) {
        const struct LkMetric *metric = &testCase->metrics[i];
        yyjson_mut_val *entry;

        if (!metricSelected(metric->name, filter)) {
            continue;
        }
        entry = yyjson_mut_obj(doc);
        if (entry == NULL ||
            !yyjson_mut_obj_add_str(doc, entry, "name", metric->name) ||
            !yyjson_mut_obj_add_str(doc, entry, "unit", metric->unit) ||
            !yyjson_mut_obj_add_real(doc, entry, "value",
                                     metric->compute(samples)) ||
            !yyjson_mut_arr_append(metrics, entry)) {
            return -1;
        }
    }

    return yyjson_mut_obj_add_val(doc, item, "metrics", metrics) ? 0 : -1;
}

static int appendJsonResult(yyjson_mut_doc *doc, yyjson_mut_val *results,
                            const struct LkTestCase *testCase,
                            const struct LkSamples *samples,
                            const char *filter) {
    yyjson_mut_val *item = yyjson_mut_obj(doc);

    if (item == NULL) {
        return -1;
    }
    if (!yyjson_mut_obj_add_str(doc, item, "id", testCase->id) ||
        !yyjson_mut_obj_add_int(doc, item, "samples", (int)samples->count) ||
        appendJsonMetrics(doc, item, testCase, samples, filter) != 0 ||
        !yyjson_mut_arr_append(results, item)) {
        return -1;
    }
    return 0;
}

static void printTextResult(const struct LkTestCase *testCase,
                            const struct LkSamples *samples, const char *filter,
                            size_t idWidth) {
    printf("%-*s", (int)idWidth, testCase->id);

    for (size_t i = 0; i < testCase->metricCount; i++) {
        const struct LkMetric *metric = &testCase->metrics[i];

        if (!metricSelected(metric->name, filter)) {
            continue;
        }
        printf("  %s %9.1f %s", metric->name, metric->compute(samples),
               metric->unit);
    }

    printf("  (samples %zu)\n", samples->count);
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
    const char *metricFilter = NULL;
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
            if (validateMetricFilter(optarg) != 0) {
                fprintf(stderr, "%s: invalid stats list '%s'\n", program,
                        optarg);
                printUsage(stderr, program);
                return 2;
            }
            metricFilter = optarg;
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
    if (metricFilter != NULL && metricFilterKnown(metricFilter, count) == 0) {
        fprintf(stderr, "%s: invalid stats list '%s'\n", program, metricFilter);
        printUsage(stderr, program);
        return 2;
    }

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
        struct LkSamples samples;

        if (selected != NULL && selected[i] == 0) {
            continue;
        }
        if (lkRunTestCase(testCase, &runOptions, &samples) != 0) {
            failures++;
            continue;
        }

        if (format == LkFormatJson) {
            if (appendJsonResult(jsonDoc, jsonResults, testCase, &samples,
                                 metricFilter) != 0) {
                fprintf(stderr, "%s: failed to build JSON output\n", program);
                lkFreeSamples(&samples);
                free(selected);
                yyjson_mut_doc_free(jsonDoc);
                return 1;
            }
        } else {
            printTextResult(testCase, &samples, metricFilter, idWidth);
        }
        lkFreeSamples(&samples);
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
