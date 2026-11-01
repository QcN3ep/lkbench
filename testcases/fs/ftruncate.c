/* SPDX-License-Identifier: MPL-2.0 */

#include "lkbench.h"

#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <unistd.h>

static char path[128];
static int fileFd = -1;
static int currentSize;
static volatile long sink;

static int start(void) {
    if (snprintf(path, sizeof(path), "/tmp/lkbench-XXXXXX") < 0) {
        return -1;
    }
    fileFd = mkstemp(path);
    if (fileFd < 0) {
        return -1;
    }
    return ftruncate(fileFd, 0) == 0 ? 0 : -1;
}

static void test(void) {
    currentSize = currentSize == 0 ? 4096 : 0;
    sink = (long)ftruncate(fileFd, (off_t)currentSize);
}

static void terminate(void) {
    if (fileFd >= 0) {
        close(fileFd);
    }
    unlink(path);
}

LK_TEST()
