/* SPDX-License-Identifier: MPL-2.0 */

#include "lkbench.h"

#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

static char dir[128];
static char pathA[160];
static char pathB[160];
static volatile long sink;

static int start(void) {
    int fd;

    if (snprintf(dir, sizeof(dir), "/tmp/lkbench-XXXXXX") < 0) {
        return -1;
    }
    if (mkdtemp(dir) == NULL) {
        return -1;
    }
    if (snprintf(pathA, sizeof(pathA), "%s/a", dir) < 0 ||
        snprintf(pathB, sizeof(pathB), "%s/b", dir) < 0) {
        return -1;
    }

    fd = open(pathA, O_CREAT | O_WRONLY, 0600);
    if (fd < 0) {
        return -1;
    }
    close(fd);
    return 0;
}

static void test(void) {
    sink = (long)rename(pathA, pathB);
    sink = (long)rename(pathB, pathA);
}

static void terminate(void) {
    unlink(pathA);
    unlink(pathB);
    rmdir(dir);
}

LK_TEST()
