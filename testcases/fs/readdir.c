/* SPDX-License-Identifier: MPL-2.0 */

#include "lkbench.h"

#include <dirent.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#define ENTRY_COUNT 8

static char dir[128];
static char names[ENTRY_COUNT][160];
static volatile long sink;

static int start(void) {
    if (snprintf(dir, sizeof(dir), "/tmp/lkbench-XXXXXX") < 0) {
        return -1;
    }
    if (mkdtemp(dir) == NULL) {
        return -1;
    }

    for (int i = 0; i < ENTRY_COUNT; i++) {
        int fd;

        if (snprintf(names[i], sizeof(names[i]), "%s/f%d", dir, i) < 0) {
            return -1;
        }
        fd = open(names[i], O_CREAT | O_WRONLY, 0600);
        if (fd < 0) {
            return -1;
        }
        close(fd);
    }

    return 0;
}

static void test(void) {
    DIR *stream = opendir(dir);
    struct dirent *entry;

    if (stream == NULL) {
        return;
    }
    while ((entry = readdir(stream)) != NULL) {
        sink++;
    }
    closedir(stream);
}

static void terminate(void) {
    for (int i = 0; i < ENTRY_COUNT; i++) {
        unlink(names[i]);
    }
    rmdir(dir);
}

LK_METRICS_LATENCY()
LK_TEST()
