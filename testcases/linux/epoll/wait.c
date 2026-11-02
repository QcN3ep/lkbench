/* SPDX-License-Identifier: MPL-2.0 */

#include "lkbench.h"

#include <stdint.h>
#include <sys/epoll.h>
#include <sys/eventfd.h>
#include <unistd.h>

static int epollFd = -1;
static int eventFd = -1;
static volatile long sink;

static int start(void) {
    struct epoll_event event = {0};

    eventFd = eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);
    if (eventFd < 0) {
        return -1;
    }
    epollFd = epoll_create1(EPOLL_CLOEXEC);
    if (epollFd < 0) {
        return -1;
    }

    event.events = EPOLLIN;
    event.data.fd = eventFd;
    if (epoll_ctl(epollFd, EPOLL_CTL_ADD, eventFd, &event) != 0) {
        return -1;
    }
    return 0;
}

static void test(void) {
    uint64_t one = 1;
    uint64_t value;
    struct epoll_event event;

    if (write(eventFd, &one, sizeof(one)) == (ssize_t)sizeof(one) &&
        epoll_wait(epollFd, &event, 1, -1) > 0) {
        sink = (long)read(eventFd, &value, sizeof(value));
    }
}

static void terminate(void) {
    if (epollFd >= 0) {
        close(epollFd);
    }
    if (eventFd >= 0) {
        close(eventFd);
    }
}

LK_METRICS_LATENCY()
LK_TEST()
