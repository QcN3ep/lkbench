/* SPDX-License-Identifier: MPL-2.0 */

#include "lkbench.h"

#include "futex_util.h"

static struct FutexShared *shared;
static pid_t childPid = -1;
static volatile long sink;

static int start(void) {
    shared = futexSharedCreate();
    if (shared == NULL) {
        return -1;
    }

    childPid = fork();
    if (childPid < 0) {
        return -1;
    }
    if (childPid == 0) {
        futexChildLoop(shared);
    }
    return 0;
}

static void test(void) {
    atomic_store_explicit(&shared->response, 0, memory_order_release);
    atomic_store_explicit(&shared->request, 1, memory_order_release);
    (void)futexWake(&shared->request, 1);

    while (atomic_load_explicit(&shared->response, memory_order_acquire) == 0) {
        (void)futexWait(&shared->response, 0);
    }
    sink = 1;
}

static void terminate(void) {
    futexStopChild(shared, childPid);
    futexSharedDestroy(shared);
    shared = NULL;
}

LK_METRICS_LATENCY()
LK_TEST()
