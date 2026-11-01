/* SPDX-License-Identifier: MPL-2.0 */

#include "lkbench.h"

#include <sys/utsname.h>

static volatile long sink;

static int start(void) { return 0; }

static void test(void) {
    struct utsname info;

    sink = (long)uname(&info);
}

static void terminate(void) {}

LK_METRICS_LATENCY()
LK_TEST()
