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
static void *address;
static int writable = 1;
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
    if (ftruncate(fileFd, (off_t)pageSize) != 0) {
        return -1;
    }

    address =
        mmap(NULL, pageSize, PROT_READ | PROT_WRITE, MAP_SHARED, fileFd, 0);
    if (address == MAP_FAILED) {
        address = NULL;
        return -1;
    }
    return 0;
}

static void test(void) {
    writable = writable == 0 ? 1 : 0;
    sink = (long)mprotect(address, pageSize,
                          writable != 0 ? (PROT_READ | PROT_WRITE) : PROT_READ);
}

static void terminate(void) {
    if (address != NULL) {
        munmap(address, pageSize);
    }
    if (fileFd >= 0) {
        close(fileFd);
    }
    unlink(path);
}

LK_METRICS_LATENCY()
LK_TEST()
