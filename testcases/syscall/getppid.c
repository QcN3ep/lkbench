/* SPDX-License-Identifier: MPL-2.0 */

#include "lkbench.h"

#include <unistd.h>

static volatile long sink;

static int start(void) { return 0; }

static void test(void) { sink = (long)getppid(); }

static void terminate(void) {}

LK_TEST()
