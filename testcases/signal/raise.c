/* SPDX-License-Identifier: MPL-2.0 */

#include "lkbench.h"

#include <signal.h>
#include <string.h>

static struct sigaction oldAction;
static int actionSaved;
static volatile sig_atomic_t raised;
static volatile long sink;

static void handler(int signalNumber) {
    (void)signalNumber;
    raised = 1;
}

static int start(void) {
    struct sigaction action;

    memset(&action, 0, sizeof(action));
    action.sa_handler = handler;
    sigemptyset(&action.sa_mask);
    if (sigaction(SIGUSR1, &action, &oldAction) != 0) {
        return -1;
    }
    actionSaved = 1;
    return 0;
}

static void test(void) {
    raised = 0;
    sink = (long)raise(SIGUSR1);
    sink = (long)raised;
}

static void terminate(void) {
    if (actionSaved != 0) {
        (void)sigaction(SIGUSR1, &oldAction, NULL);
    }
}

LK_METRICS_LATENCY()
LK_TEST()
