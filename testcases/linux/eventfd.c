/* SPDX-License-Identifier: MPL-2.0 */

#include "lkbench.h"

#include <stdint.h>
#include <sys/eventfd.h>
#include <unistd.h>

static int eventFd = -1;
static volatile long sink;

static int start(void) {
    eventFd = eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);
    return eventFd < 0 ? -1 : 0;
}

static void test(void) {
    uint64_t one = 1;
    uint64_t value;

    if (write(eventFd, &one, sizeof(one)) == (ssize_t)sizeof(one)) {
        sink = (long)read(eventFd, &value, sizeof(value));
    }
}

static void terminate(void) {
    if (eventFd >= 0) {
        close(eventFd);
    }
}

LK_METRICS_LATENCY()
LK_TEST()
