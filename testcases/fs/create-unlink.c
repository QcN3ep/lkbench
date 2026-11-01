/* SPDX-License-Identifier: MPL-2.0 */

#include "lkbench.h"

#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

static char dir[128];
static char path[160];
static volatile long sink;

static int start(void) {
    if (snprintf(dir, sizeof(dir), "/tmp/lkbench-XXXXXX") < 0) {
        return -1;
    }
    if (mkdtemp(dir) == NULL) {
        return -1;
    }
    if (snprintf(path, sizeof(path), "%s/file", dir) < 0) {
        return -1;
    }
    return 0;
}

static void test(void) {
    int fd = open(path, O_CREAT | O_EXCL | O_WRONLY, 0600);

    if (fd >= 0) {
        sink = (long)close(fd);
    }
    sink = (long)unlink(path);
}

static void terminate(void) {
    unlink(path);
    rmdir(dir);
}

LK_TEST()
