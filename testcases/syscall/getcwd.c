/* SPDX-License-Identifier: MPL-2.0 */

#include "lkbench.h"

#include <unistd.h>

static char buffer[256];
static volatile long sink;

static int start(void) { return 0; }

static void test(void) {
    sink = (long)(getcwd(buffer, sizeof(buffer)) != NULL);
}

static void terminate(void) {}

LK_TEST()
