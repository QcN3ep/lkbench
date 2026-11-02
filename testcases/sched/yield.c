/* SPDX-License-Identifier: MPL-2.0 */

#include "lkbench.h"

#include <sched.h>

static volatile long sink;

static int start(void) { return 0; }

static void test(void) { sink = (long)sched_yield(); }

static void terminate(void) {}

LK_METRICS_LATENCY()
LK_TEST()
