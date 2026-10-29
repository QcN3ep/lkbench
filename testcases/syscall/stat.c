/* SPDX-License-Identifier: MPL-2.0 */

#include "lkbench.h"

#include <sys/stat.h>

static volatile long sink;

static int start(void) { return 0; }

static void test(void) {
    struct stat info;

    sink = (long)stat(".", &info);
}

static void terminate(void) {}

LK_TEST()
