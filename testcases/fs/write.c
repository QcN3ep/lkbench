/* SPDX-License-Identifier: MPL-2.0 */

#include "lkbench.h"

#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <unistd.h>

#define BUFFER_SIZE 4096

static char path[128];
static char buffer[BUFFER_SIZE];
static int fileFd = -1;
static volatile long sink;

static int start(void) {
    if (snprintf(path, sizeof(path), "/tmp/lkbench-XXXXXX") < 0) {
        return -1;
    }
    fileFd = mkstemp(path);
    if (fileFd < 0) {
        return -1;
    }
    return ftruncate(fileFd, (off_t)BUFFER_SIZE) == 0 ? 0 : -1;
}

static void test(void) {
    sink = (long)pwrite(fileFd, buffer, sizeof(buffer), 0);
}

static void terminate(void) {
    if (fileFd >= 0) {
        close(fileFd);
    }
    unlink(path);
}

LK_TEST()
