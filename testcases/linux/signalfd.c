/* SPDX-License-Identifier: MPL-2.0 */

#include "lkbench.h"

#include <signal.h>
#include <sys/signalfd.h>
#include <unistd.h>

static int signalFd = -1;
static sigset_t oldMask;
static volatile long sink;

static int start(void) {
    sigset_t mask;

    sigemptyset(&mask);
    sigaddset(&mask, SIGUSR1);
    if (sigprocmask(SIG_BLOCK, &mask, &oldMask) != 0) {
        return -1;
    }

    signalFd = signalfd(-1, &mask, SFD_NONBLOCK | SFD_CLOEXEC);
    return signalFd < 0 ? -1 : 0;
}

static void test(void) {
    struct signalfd_siginfo info;

    if (raise(SIGUSR1) == 0) {
        sink = (long)read(signalFd, &info, sizeof(info));
    }
}

static void terminate(void) {
    if (signalFd >= 0) {
        close(signalFd);
    }
    (void)sigprocmask(SIG_SETMASK, &oldMask, NULL);
}

LK_METRICS_LATENCY()
LK_TEST()
