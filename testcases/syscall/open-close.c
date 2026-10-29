/* SPDX-License-Identifier: MPL-2.0 */

#include "lkbench.h"

#include <fcntl.h>
#include <unistd.h>

static volatile long sink;

static int start(void) { return 0; }

static void test(void) {
    int fd = open("/dev/null", O_RDONLY);

    if (fd >= 0) {
        sink = (long)close(fd);
    }
}

static void terminate(void) {}

LK_TEST()
