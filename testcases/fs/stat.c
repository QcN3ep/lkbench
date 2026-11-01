/* SPDX-License-Identifier: MPL-2.0 */

#include "lkbench.h"

#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <unistd.h>

static char path[128];
static volatile long sink;

static int start(void) {
    int fd;

    if (snprintf(path, sizeof(path), "/tmp/lkbench-XXXXXX") < 0) {
        return -1;
    }
    fd = mkstemp(path);
    if (fd < 0) {
        return -1;
    }
    close(fd);
    return 0;
}

static void test(void) {
    struct stat info;

    sink = (long)stat(path, &info);
}

static void terminate(void) { unlink(path); }

LK_METRICS_LATENCY()
LK_TEST()
