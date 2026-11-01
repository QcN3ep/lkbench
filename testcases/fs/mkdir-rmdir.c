/* SPDX-License-Identifier: MPL-2.0 */

#include "lkbench.h"

#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <unistd.h>

static char dir[128];
static char child[160];
static volatile long sink;

static int start(void) {
    if (snprintf(dir, sizeof(dir), "/tmp/lkbench-XXXXXX") < 0) {
        return -1;
    }
    if (mkdtemp(dir) == NULL) {
        return -1;
    }
    return snprintf(child, sizeof(child), "%s/child", dir) < 0 ? -1 : 0;
}

static void test(void) {
    sink = (long)mkdir(child, 0700);
    sink = (long)rmdir(child);
}

static void terminate(void) {
    rmdir(child);
    rmdir(dir);
}

LK_TEST()
