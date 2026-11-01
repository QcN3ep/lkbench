/* SPDX-License-Identifier: MPL-2.0 */

#include "lkbench.h"

#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <unistd.h>

static char path[128];
static int fileFd = -1;
static volatile long sink;

static int start(void) {
    if (snprintf(path, sizeof(path), "/tmp/lkbench-XXXXXX") < 0) {
        return -1;
    }
    fileFd = mkstemp(path);
    return fileFd < 0 ? -1 : 0;
}

static void test(void) {
    struct stat info;

    sink = (long)fstat(fileFd, &info);
}

static void terminate(void) {
    if (fileFd >= 0) {
        close(fileFd);
    }
    unlink(path);
}

LK_TEST()
