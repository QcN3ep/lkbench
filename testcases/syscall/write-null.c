/* SPDX-License-Identifier: MPL-2.0 */

#include "lkbench.h"

#include <fcntl.h>
#include <unistd.h>

static int nullFd = -1;
static volatile long sink;

static int start(void) {
    nullFd = open("/dev/null", O_WRONLY);
    return nullFd < 0 ? -1 : 0;
}

static void test(void) {
    char byte = 0;

    sink = (long)write(nullFd, &byte, 1);
}

static void terminate(void) {
    if (nullFd >= 0) {
        sink = (long)close(nullFd);
    }
}

LK_TEST()
