/* SPDX-License-Identifier: MPL-2.0 */

#include "lkbench.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAX_TEST_CASES 256

static struct LkTestCase *testCases[MAX_TEST_CASES];
static size_t testCaseCount;
static int registrySorted;

static const char *baseName(const char *path) {
    const char *slash = strrchr(path, '/');

    return slash != NULL ? slash + 1 : path;
}

static const char *testcasesRelative(const char *path) {
    const char *found = NULL;
    const char *cursor = path;

    for (;;) {
        const char *match = strstr(cursor, "testcases/");

        if (match == NULL) {
            break;
        }
        if (match == path || match[-1] == '/') {
            found = match + strlen("testcases/");
        }
        cursor = match + strlen("testcases/");
    }

    return found;
}

static void deriveId(struct LkTestCase *testCase) {
    const char *begin = testcasesRelative(testCase->file);

    if (begin == NULL) {
        begin = baseName(testCase->file);
    }
    size_t length = strlen(begin);

    if (length >= 2 && strcmp(begin + length - 2, ".c") == 0) {
        length -= 2;
    }
    if (length == 0 || length >= sizeof(testCase->id)) {
        fprintf(stderr, "lkbench: invalid test id for %s\n", testCase->file);
        exit(1);
    }

    for (size_t i = 0; i < length; i++) {
        testCase->id[i] = begin[i] == '/' ? '.' : begin[i];
    }
    testCase->id[length] = '\0';
}

static int compareIds(const void *leftValue, const void *rightValue) {
    const struct LkTestCase *left =
        *(const struct LkTestCase *const *)leftValue;
    const struct LkTestCase *right =
        *(const struct LkTestCase *const *)rightValue;

    return strcmp(left->id, right->id);
}

static void sortRegistry(void) {
    if (registrySorted == 0) {
        qsort((void *)testCases, testCaseCount, sizeof(testCases[0]),
              compareIds);
        registrySorted = 1;
    }
}

void lkRegisterTestCase(struct LkTestCase *testCase) {
    deriveId(testCase);

    for (size_t i = 0; i < testCaseCount; i++) {
        if (strcmp(testCases[i]->id, testCase->id) == 0) {
            fprintf(stderr, "lkbench: duplicate test id '%s'\n", testCase->id);
            exit(1);
        }
    }

    if (testCaseCount >= MAX_TEST_CASES) {
        fprintf(stderr, "lkbench: too many test cases\n");
        exit(1);
    }

    testCases[testCaseCount] = testCase;
    testCaseCount++;
    registrySorted = 0;
}

size_t lkTestCaseCount(void) {
    sortRegistry();
    return testCaseCount;
}

struct LkTestCase *lkTestCaseAt(size_t index) {
    sortRegistry();
    return index < testCaseCount ? testCases[index] : NULL;
}

int lkTestCaseMatches(const struct LkTestCase *testCase, const char *selector) {
    size_t length = strlen(selector);

    if (strcmp(testCase->id, selector) == 0) {
        return 1;
    }

    return length > 0 && strncmp(testCase->id, selector, length) == 0 &&
           testCase->id[length] == '.';
}
