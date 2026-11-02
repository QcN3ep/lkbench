/* SPDX-License-Identifier: MPL-2.0 */

#include "lkbench.h"

#include <stdint.h>
#include <sys/timerfd.h>
#include <time.h>
#include <unistd.h>

static int timerFd = -1;
static struct itimerspec timer;
static volatile long sink;

static int start(void) {
    timerFd = timerfd_create(CLOCK_MONOTONIC, TFD_CLOEXEC);
    if (timerFd < 0) {
        return -1;
    }
    if (clock_gettime(CLOCK_MONOTONIC, &timer.it_value) != 0) {
        return -1;
    }
    if (timer.it_value.tv_sec > 0) {
        timer.it_value.tv_sec--;
    } else {
        timer.it_value.tv_nsec = 1;
    }
    return 0;
}

static void test(void) {
    uint64_t expirations;

    if (timerfd_settime(timerFd, TFD_TIMER_ABSTIME, &timer, NULL) == 0) {
        sink = (long)read(timerFd, &expirations, sizeof(expirations));
    }
}

static void terminate(void) {
    if (timerFd >= 0) {
        close(timerFd);
    }
}

LK_METRICS_LATENCY()
LK_TEST()
