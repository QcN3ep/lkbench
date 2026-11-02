/* SPDX-License-Identifier: MPL-2.0 */

#include "lkbench.h"

#include <YMLParser.h>
#include <errno.h>
#include <getopt.h>
#include <minicbor.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>
#include <xml.h>
#include <yyjson.h>

#define MAX_SELECTORS 64
#define MAX_TEXT_COLUMNS 32
#define MAX_TEXT_GROUPS 16
#define TEXT_COLUMN_WIDTH 12
#define TEXT_SAMPLES_WIDTH 10

enum LkFormat {
    LkFormatText,
    LkFormatJson,
    LkFormatXml,
    LkFormatYaml,
    LkFormatCbor,
};

struct TextColumn {
    const char *name;
    size_t width;
    char label[64];
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
    fprintf(stream, "  -f, --format <format>  Output format: text (default), "
                    "json, xml, yaml, cbor\n");
    fprintf(stream, "  -w, --warmup <duration>  Warmup time (default 100ms)\n");
    fprintf(stream,
            "  -n, --samples <count>    Samples per test (default 100000)\n");
    fprintf(stream,
            "  -s, --stats <list>       Metrics to output (comma separated)\n");
    fprintf(stream, "  -q, --no-progress       Disable progress bar\n");
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
    if (strcmp(value, "xml") == 0) {
        *format = LkFormatXml;
        return 0;
    }
    if (strcmp(value, "yaml") == 0 || strcmp(value, "yml") == 0) {
        *format = LkFormatYaml;
        return 0;
    }
    if (strcmp(value, "cbor") == 0) {
        *format = LkFormatCbor;
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

struct TextGroup {
    char name[64];
    struct TextColumn columns[MAX_TEXT_COLUMNS];
    size_t columnCount;
};

static void testGroupName(const char *id, char *buffer, size_t size) {
    const char *dot = strchr(id, '.');
    size_t length = dot != NULL ? (size_t)(dot - id) : strlen(id);

    if (length >= size) {
        length = size - 1;
    }
    memcpy(buffer, id, length);
    buffer[length] = '\0';
}

static struct TextGroup *findTextGroup(struct TextGroup *groups, size_t count,
                                       const char *name) {
    for (size_t i = 0; i < count; i++) {
        if (strcmp(groups[i].name, name) == 0) {
            return &groups[i];
        }
    }
    return NULL;
}

static int addTextColumn(struct TextColumn *columns, size_t *columnCount,
                         const struct LkMetric *metric, const char *filter) {
    for (size_t i = 0; i < *columnCount; i++) {
        if (strcmp(columns[i].name, metric->name) == 0) {
            return 0;
        }
    }
    if (!metricSelected(metric->name, filter)) {
        return 0;
    }
    if (*columnCount >= MAX_TEXT_COLUMNS) {
        return -1;
    }

    struct TextColumn *column = &columns[*columnCount];

    column->name = metric->name;
    (void)snprintf(column->label, sizeof(column->label), "%s(%s)", metric->name,
                   metric->unit);
    column->width = strlen(column->label);
    if (column->width < TEXT_COLUMN_WIDTH) {
        column->width = TEXT_COLUMN_WIDTH;
    }
    (*columnCount)++;
    return 0;
}

static int buildTextGroups(const unsigned char *selected, size_t testCount,
                           const char *filter, struct TextGroup *groups,
                           size_t *groupCount) {
    *groupCount = 0;

    for (size_t i = 0; i < testCount; i++) {
        const struct LkTestCase *testCase = lkTestCaseAt(i);
        char groupName[64];
        struct TextGroup *group;

        if (selected != NULL && selected[i] == 0) {
            continue;
        }

        testGroupName(testCase->id, groupName, sizeof(groupName));
        group = findTextGroup(groups, *groupCount, groupName);
        if (group == NULL) {
            if (*groupCount >= MAX_TEXT_GROUPS) {
                return -1;
            }
            group = &groups[*groupCount];
            (void)snprintf(group->name, sizeof(group->name), "%s", groupName);
            group->columnCount = 0;
            (*groupCount)++;
        }

        for (size_t j = 0; j < testCase->metricCount; j++) {
            if (addTextColumn(group->columns, &group->columnCount,
                              &testCase->metrics[j], filter) != 0) {
                return -1;
            }
        }
    }
    return 0;
}

static const struct LkMetric *findMetric(const struct LkTestCase *testCase,
                                         const char *name) {
    for (size_t i = 0; i < testCase->metricCount; i++) {
        if (strcmp(testCase->metrics[i].name, name) == 0) {
            return &testCase->metrics[i];
        }
    }
    return NULL;
}

static void printTextHeader(size_t idWidth, const struct TextColumn *columns,
                            size_t columnCount) {
    printf("%-*s", (int)idWidth, "test");
    for (size_t i = 0; i < columnCount; i++) {
        printf("  %*s", (int)columns[i].width, columns[i].label);
    }
    printf("  %*s\n", TEXT_SAMPLES_WIDTH, "samples");
}

static void printTextRow(const struct LkTestCase *testCase,
                         const struct LkSamples *samples, size_t idWidth,
                         const struct TextColumn *columns, size_t columnCount) {
    printf("%-*s", (int)idWidth, testCase->id);

    for (size_t i = 0; i < columnCount; i++) {
        const struct LkMetric *metric = findMetric(testCase, columns[i].name);

        if (metric != NULL) {
            printf("  %*.1f", (int)columns[i].width, metric->compute(samples));
        } else {
            printf("  %*s", (int)columns[i].width, "N/A");
        }
    }

    printf("  %*zu\n", TEXT_SAMPLES_WIDTH, samples->count);
}

static char *xmlEscapeAttr(const char *value) {
    XMLString *escaped = xml_string_new();

    if (escaped == NULL) {
        return NULL;
    }
    for (const char *cursor = value; *cursor != '\0'; cursor++) {
        switch (*cursor) {
        case '&':
            xml_string_append(escaped, "&amp;");
            break;
        case '<':
            xml_string_append(escaped, "&lt;");
            break;
        case '>':
            xml_string_append(escaped, "&gt;");
            break;
        case '"':
            xml_string_append(escaped, "&quot;");
            break;
        case '\'':
            xml_string_append(escaped, "&apos;");
            break;
        default: {
            char character[2] = {*cursor, '\0'};
            xml_string_append(escaped, character);
            break;
        }
        }
    }
    return xml_string_steal(escaped);
}

static void addXmlAttr(XMLNode *node, const char *key, const char *value) {
    char *escaped = xmlEscapeAttr(value);

    if (escaped != NULL) {
        xml_node_add_attr(node, key, escaped);
        free(escaped);
    }
}

static int appendXmlResult(XMLNode *root, const struct LkTestCase *testCase,
                           const struct LkSamples *samples,
                           const char *filter) {
    XMLNode *testNode = xml_node_new(root, "test", NULL);
    char buffer[64];

    if (testNode == NULL) {
        return -1;
    }
    addXmlAttr(testNode, "id", testCase->id);
    (void)snprintf(buffer, sizeof(buffer), "%zu", samples->count);
    addXmlAttr(testNode, "samples", buffer);

    for (size_t i = 0; i < testCase->metricCount; i++) {
        const struct LkMetric *metric = &testCase->metrics[i];
        XMLNode *metricNode;

        if (!metricSelected(metric->name, filter)) {
            continue;
        }
        metricNode = xml_node_new(testNode, "metric", NULL);
        if (metricNode == NULL) {
            return -1;
        }
        addXmlAttr(metricNode, "name", metric->name);
        addXmlAttr(metricNode, "unit", metric->unit);
        (void)snprintf(buffer, sizeof(buffer), "%.17g",
                       metric->compute(samples));
        addXmlAttr(metricNode, "value", buffer);
    }

    return 0;
}

static int appendYamlResult(YMLValue *root, const struct LkTestCase *testCase,
                            const struct LkSamples *samples,
                            const char *filter) {
    YMLValue *test = YMLCreate(NULL);
    YMLValue *metrics = YMLCreateArr(NULL);

    if (test == NULL || metrics == NULL) {
        YMLDestroy(test);
        YMLDestroy(metrics);
        return -1;
    }

    YMLMapAdd(test, "id", testCase->id);
    YMLMapAdd(test, "samples", (long long)samples->count);

    for (size_t i = 0; i < testCase->metricCount; i++) {
        const struct LkMetric *metric = &testCase->metrics[i];
        YMLValue *entry;

        if (!metricSelected(metric->name, filter)) {
            continue;
        }
        entry = YMLCreate(NULL);
        if (entry == NULL) {
            YMLDestroy(test);
            YMLDestroy(metrics);
            return -1;
        }
        YMLMapAdd(entry, "name", metric->name);
        YMLMapAdd(entry, "unit", metric->unit);
        YMLMapAdd(entry, "value", metric->compute(samples));
        YMLArrPush(metrics, entry);
        YMLDestroy(entry);
    }

    YMLMapAdd(test, "metrics", metrics);
    YMLDestroy(metrics);
    YMLArrPush(root, test);
    YMLDestroy(test);
    return 0;
}

static int appendCborResult(mcbor_encoder_t *encoder,
                            const struct LkTestCase *testCase,
                            const struct LkSamples *samples,
                            const char *filter) {
    size_t metricCount = 0;

    for (size_t i = 0; i < testCase->metricCount; i++) {
        if (metricSelected(testCase->metrics[i].name, filter)) {
            metricCount++;
        }
    }

    if (mcbor_encode_map_start(encoder, 3) != MCBOR_OK ||
        mcbor_encode_text_cstr(encoder, "id") != MCBOR_OK ||
        mcbor_encode_text_cstr(encoder, testCase->id) != MCBOR_OK ||
        mcbor_encode_text_cstr(encoder, "samples") != MCBOR_OK ||
        mcbor_encode_uint(encoder, (mcbor_uint64_t)samples->count) !=
            MCBOR_OK ||
        mcbor_encode_text_cstr(encoder, "metrics") != MCBOR_OK ||
        mcbor_encode_array_start(encoder, metricCount) != MCBOR_OK) {
        return -1;
    }

    for (size_t i = 0; i < testCase->metricCount; i++) {
        const struct LkMetric *metric = &testCase->metrics[i];

        if (!metricSelected(metric->name, filter)) {
            continue;
        }
        if (mcbor_encode_map_start(encoder, 3) != MCBOR_OK ||
            mcbor_encode_text_cstr(encoder, "name") != MCBOR_OK ||
            mcbor_encode_text_cstr(encoder, metric->name) != MCBOR_OK ||
            mcbor_encode_text_cstr(encoder, "unit") != MCBOR_OK ||
            mcbor_encode_text_cstr(encoder, metric->unit) != MCBOR_OK ||
            mcbor_encode_text_cstr(encoder, "value") != MCBOR_OK ||
            mcbor_encode_float64(encoder, metric->compute(samples)) !=
                MCBOR_OK) {
            return -1;
        }
    }

    return mcbor_encoder_error(encoder) == MCBOR_OK ? 0 : -1;
}

struct ProgressState {
    size_t current;
    size_t total;
};

static size_t progressWidth(void) {
    struct winsize window;

    if (ioctl(STDERR_FILENO, TIOCGWINSZ, &window) == 0 && window.ws_col > 0) {
        return window.ws_col;
    }
    return 80;
}

static void clearProgressLine(void) {
    size_t width = progressWidth();

    fprintf(stderr, "\r%*s\r", (int)width, "");
}

static void showProgress(void *context, const char *testId, size_t completed,
                         size_t total) {
    struct ProgressState *state = context;
    double testFraction = total > 0 ? (double)completed / (double)total : 0.0;
    double overall =
        state->total > 0
            ? ((double)state->current + testFraction) / (double)state->total
            : 0.0;
    char meta[128];
    int metaLength =
        snprintf(meta, sizeof(meta), " %3.0f%%  [%zu/%zu]  ", overall * 100.0,
                 state->current + 1, state->total);
    size_t columns = progressWidth();
    size_t idLength = strlen(testId);
    size_t fixed = 2 + (size_t)metaLength;
    size_t available = columns > fixed ? columns - fixed : 0;
    size_t barWidth;

    if (idLength > available) {
        idLength = available > 10 ? available - 10 : 0;
    }
    barWidth = available > idLength ? available - idLength : 0;
    if (barWidth == 0) {
        barWidth = 1;
        idLength = 0;
    }

    fprintf(stderr, "\r[");
    for (size_t i = 0; i < barWidth; i++) {
        fputc((double)i / (double)barWidth < overall ? '#' : '-', stderr);
    }
    fprintf(stderr, "]%s%.*s", meta, (int)idLength, testId);
    fflush(stderr);
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
        {"no-progress", no_argument, NULL, 'q'},
        {NULL, 0, NULL, 0},
    };
    const char *program = argc > 0 ? argv[0] : "lkbench";
    const char *selectors[MAX_SELECTORS];
    const char *formatValue = NULL;
    const char *metricFilter = NULL;
    size_t selectorCount = 0;
    int listOnly = 0;
    int noProgress = 0;
    int option;
    size_t count;
    size_t idWidth;
    struct TextGroup textGroups[MAX_TEXT_GROUPS];
    size_t textGroupCount = 0;
    char currentTextGroup[64] = {0};
    int firstTextTable = 1;
    struct TextGroup *textGroup = NULL;
    struct ProgressState progressState = {0, 0};
    unsigned char *selected = NULL;
    enum LkFormat format = LkFormatText;
    struct LkOptions runOptions = {
        LK_DEFAULT_WARMUP_NS,
        LK_DEFAULT_SAMPLE_COUNT,
        NULL,
        NULL,
    };
    yyjson_mut_doc *jsonDoc = NULL;
    yyjson_mut_val *jsonResults = NULL;
    XMLNode *xmlRoot = NULL;
    YMLValue *yamlRoot = NULL;
    mcbor_arena_t cborArena;
    mcbor_encoder_t cborEncoder;
    int failures = 0;

    opterr = 0;
    while ((option = getopt_long(argc, argv, ":hlvb:f:w:n:s:q", options,
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
        case 'q':
            noProgress = 1;
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

    {
        size_t selectedCount = 0;

        for (size_t i = 0; i < count; i++) {
            if (selected == NULL || selected[i] != 0) {
                selectedCount++;
            }
        }
        progressState.current = 0;
        progressState.total = selectedCount;
        if (noProgress == 0 && selectedCount > 0 &&
            isatty(STDERR_FILENO) != 0) {
            runOptions.progress = showProgress;
            runOptions.progressContext = &progressState;
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

    if (format == LkFormatXml) {
        xmlRoot = xml_node_new(NULL, "lkbench", NULL);
        if (xmlRoot == NULL) {
            fprintf(stderr, "%s: out of memory\n", program);
            free(selected);
            return 1;
        }
    }
    if (format == LkFormatYaml) {
        yamlRoot = YMLCreateArr(NULL);
        if (yamlRoot == NULL) {
            fprintf(stderr, "%s: out of memory\n", program);
            free(selected);
            return 1;
        }
    }
    if (format == LkFormatCbor) {
        mcbor_arena_init(&cborArena);
        mcbor_encoder_init_dynamic(&cborEncoder, &cborArena);
        if (mcbor_encode_array_start_indef(&cborEncoder) != MCBOR_OK) {
            fprintf(stderr, "%s: failed to build CBOR output\n", program);
            mcbor_encoder_destroy(&cborEncoder);
            mcbor_arena_destroy(&cborArena);
            free(selected);
            return 1;
        }
    }

    idWidth = maxIdWidth();
    if (format == LkFormatText &&
        buildTextGroups(selected, count, metricFilter, textGroups,
                        &textGroupCount) != 0) {
        fprintf(stderr, "%s: too many metric columns\n", program);
        free(selected);
        return 1;
    }

    for (size_t i = 0; i < count; i++) {
        struct LkTestCase *testCase = lkTestCaseAt(i);
        struct LkSamples samples;

        if (selected != NULL && selected[i] == 0) {
            continue;
        }
        if (format == LkFormatText) {
            char groupName[64];

            testGroupName(testCase->id, groupName, sizeof(groupName));
            if (strcmp(currentTextGroup, groupName) != 0) {
                textGroup =
                    findTextGroup(textGroups, textGroupCount, groupName);
                if (textGroup != NULL && textGroup->columnCount > 0) {
                    if (firstTextTable == 0) {
                        printf("\n");
                    }
                    firstTextTable = 0;
                    printf("%s:\n", textGroup->name);
                    printTextHeader(idWidth, textGroup->columns,
                                    textGroup->columnCount);
                }
                (void)snprintf(currentTextGroup, sizeof(currentTextGroup), "%s",
                               groupName);
            }
        }
        if (lkRunTestCase(testCase, &runOptions, &samples) != 0) {
            failures++;
            if (runOptions.progress != NULL) {
                clearProgressLine();
                progressState.current++;
            }
            continue;
        }

        if (runOptions.progress != NULL) {
            clearProgressLine();
            progressState.current++;
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
        } else if (format == LkFormatXml) {
            if (appendXmlResult(xmlRoot, testCase, &samples, metricFilter) !=
                0) {
                fprintf(stderr, "%s: failed to build XML output\n", program);
                lkFreeSamples(&samples);
                free(selected);
                xml_node_free(xmlRoot);
                return 1;
            }
        } else if (format == LkFormatYaml) {
            if (appendYamlResult(yamlRoot, testCase, &samples, metricFilter) !=
                0) {
                fprintf(stderr, "%s: failed to build YAML output\n", program);
                lkFreeSamples(&samples);
                free(selected);
                YMLDestroy(yamlRoot);
                return 1;
            }
        } else if (format == LkFormatCbor) {
            if (appendCborResult(&cborEncoder, testCase, &samples,
                                 metricFilter) != 0) {
                fprintf(stderr, "%s: failed to build CBOR output\n", program);
                lkFreeSamples(&samples);
                free(selected);
                mcbor_encoder_destroy(&cborEncoder);
                mcbor_arena_destroy(&cborArena);
                return 1;
            }
        } else if (textGroup != NULL && textGroup->columnCount > 0) {
            printTextRow(testCase, &samples, idWidth, textGroup->columns,
                         textGroup->columnCount);
        }
        lkFreeSamples(&samples);
    }

    if (format == LkFormatXml) {
        XMLString *xml = xml_string_new();

        if (xml == NULL) {
            fprintf(stderr, "%s: out of memory\n", program);
            xml_node_free(xmlRoot);
            free(selected);
            return 1;
        }
        xml_node_serialize(xmlRoot, xml);
        printf("<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n%s\n", xml->str);
        xml_string_free(xml);
        xml_node_free(xmlRoot);
    }

    if (format == LkFormatYaml) {
        YMLWriteStream(yamlRoot, stdout);
        YMLDestroy(yamlRoot);
    }

    if (format == LkFormatCbor) {
        const unsigned char *data;
        size_t length;

        if (mcbor_encode_break(&cborEncoder) != MCBOR_OK ||
            mcbor_encoder_error(&cborEncoder) != MCBOR_OK) {
            fprintf(stderr, "%s: failed to build CBOR output\n", program);
            mcbor_encoder_destroy(&cborEncoder);
            mcbor_arena_destroy(&cborArena);
            free(selected);
            return 1;
        }
        data = mcbor_encoder_data(&cborEncoder);
        length = mcbor_encoder_len(&cborEncoder);
        if (length > 0 && fwrite(data, 1, length, stdout) != length) {
            fprintf(stderr, "%s: failed to write CBOR output\n", program);
            mcbor_encoder_destroy(&cborEncoder);
            mcbor_arena_destroy(&cborArena);
            free(selected);
            return 1;
        }
        if (fflush(stdout) != 0) {
            fprintf(stderr, "%s: failed to flush CBOR output\n", program);
            mcbor_encoder_destroy(&cborEncoder);
            mcbor_arena_destroy(&cborArena);
            free(selected);
            return 1;
        }
        mcbor_encoder_destroy(&cborEncoder);
        mcbor_arena_destroy(&cborArena);
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
