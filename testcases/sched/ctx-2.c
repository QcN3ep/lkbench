/* SPDX-License-Identifier: MPL-2.0 */

#include "lkbench.h"

#include <sched.h>
#include <signal.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

struct Shared {
    atomic_int ping;
    atomic_int pong;
    atomic_int stop;
};

static char path[128];
static int fileFd = -1;
static struct Shared *shared;
static pid_t childPid = -1;
static volatile long sink;

static int start(void) {
    if (snprintf(path, sizeof(path), "/tmp/lkbench-XXXXXX") < 0) {
        return -1;
    }
    fileFd = mkstemp(path);
    if (fileFd < 0) {
        return -1;
    }
    if (ftruncate(fileFd, (off_t)sizeof(*shared)) != 0) {
        return -1;
    }

    shared = mmap(NULL, sizeof(*shared), PROT_READ | PROT_WRITE, MAP_SHARED,
                  fileFd, 0);
    if (shared == MAP_FAILED) {
        shared = NULL;
        return -1;
    }

    atomic_init(&shared->ping, 0);
    atomic_init(&shared->pong, 0);
    atomic_init(&shared->stop, 0);

    childPid = fork();
    if (childPid < 0) {
        return -1;
    }
    if (childPid == 0) {
        for (;;) {
            if (atomic_load_explicit(&shared->stop, memory_order_acquire) !=
                0) {
                _exit(0);
            }
            if (atomic_load_explicit(&shared->ping, memory_order_acquire) !=
                0) {
                atomic_store_explicit(&shared->ping, 0, memory_order_release);
                atomic_store_explicit(&shared->pong, 1, memory_order_release);
            } else {
                sched_yield();
            }
        }
    }

    return 0;
}

static void test(void) {
    atomic_store_explicit(&shared->pong, 0, memory_order_release);
    atomic_store_explicit(&shared->ping, 1, memory_order_release);

    while (atomic_load_explicit(&shared->pong, memory_order_acquire) == 0) {
        sched_yield();
    }
    sink = 1;
}

static void terminate(void) {
    if (shared != NULL) {
        atomic_store_explicit(&shared->stop, 1, memory_order_release);
        atomic_store_explicit(&shared->ping, 1, memory_order_release);
    }
    if (childPid > 0) {
        kill(childPid, SIGTERM);
        waitpid(childPid, NULL, 0);
    }
    if (shared != NULL) {
        munmap(shared, sizeof(*shared));
    }
    if (fileFd >= 0) {
        close(fileFd);
    }
    unlink(path);
}

LK_METRICS_LATENCY()
LK_TEST()
