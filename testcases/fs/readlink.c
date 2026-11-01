/* SPDX-License-Identifier: MPL-2.0 */

#include "lkbench.h"

#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

static char dir[128];
static char target[160];
static char linkPath[160];
static char buffer[256];
static volatile long sink;

static int start(void) {
    int fd;

    if (snprintf(dir, sizeof(dir), "/tmp/lkbench-XXXXXX") < 0) {
        return -1;
    }
    if (mkdtemp(dir) == NULL) {
        return -1;
    }
    if (snprintf(target, sizeof(target), "%s/target", dir) < 0 ||
        snprintf(linkPath, sizeof(linkPath), "%s/link", dir) < 0) {
        return -1;
    }

    fd = open(target, O_CREAT | O_WRONLY, 0600);
    if (fd < 0) {
        return -1;
    }
    close(fd);
    return symlink("target", linkPath) == 0 ? 0 : -1;
}

static void test(void) {
    sink = (long)readlink(linkPath, buffer, sizeof(buffer));
}

static void terminate(void) {
    unlink(linkPath);
    unlink(target);
    rmdir(dir);
}

LK_TEST()
