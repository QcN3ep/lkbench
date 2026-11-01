/* SPDX-License-Identifier: MPL-2.0 */

#include "lkbench.h"

#include <stdio.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <sys/types.h>
#include <unistd.h>

static char path[128];
static size_t pageSize;
static int fileFd = -1;
static volatile long sink;

static int start(void) {
    long page = sysconf(_SC_PAGESIZE);

    if (page <= 0) {
        return -1;
    }
    pageSize = (size_t)page;

    if (snprintf(path, sizeof(path), "/tmp/lkbench-XXXXXX") < 0) {
        return -1;
    }
    fileFd = mkstemp(path);
    if (fileFd < 0) {
        return -1;
    }
    return ftruncate(fileFd, (off_t)pageSize) == 0 ? 0 : -1;
}

static void test(void) {
    void *address = mmap(NULL, pageSize, PROT_READ, MAP_SHARED, fileFd, 0);

    if (address != MAP_FAILED) {
        sink = (long)((const unsigned char *)address)[0];
        munmap(address, pageSize);
    }
}

static void terminate(void) {
    if (fileFd >= 0) {
        close(fileFd);
    }
    unlink(path);
}

LK_METRICS_LATENCY()
LK_TEST()
