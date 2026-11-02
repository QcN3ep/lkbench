/* SPDX-License-Identifier: MPL-2.0 */

#include "lkbench.h"

#include "io_uring_util.h"

#include <fcntl.h>

static struct IoUring ring;
static int fd = -1;
static char buffer[1];
static volatile long sink;

static int start(void) {
    if (ioUringInit(&ring, 2) != 0) {
        return -1;
    }
    fd = open("/dev/zero", O_RDONLY | O_CLOEXEC);
    if (fd < 0) {
        return -1;
    }
    return 0;
}

static void test(void) {
    int result = 0;

    if (ioUringSubmit(&ring, IORING_OP_READ, fd, buffer, sizeof(buffer),
                      &result) == 0) {
        sink = result;
    }
}

static void terminate(void) {
    if (fd >= 0) {
        close(fd);
    }
    ioUringDestroy(&ring);
}

LK_METRICS_LATENCY()
LK_TEST_OPTIONAL(ioUringProbe)
