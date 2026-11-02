/* SPDX-License-Identifier: MPL-2.0 */

#ifndef LKBENCH_FUTEX_UTIL_H
#define LKBENCH_FUTEX_UTIL_H

#include <linux/futex.h>
#include <sched.h>
#include <signal.h>
#include <stdatomic.h>
#include <sys/mman.h>
#include <sys/syscall.h>
#include <sys/wait.h>
#include <unistd.h>

struct FutexShared {
    atomic_int request;
    atomic_int response;
    atomic_int stop;
};

static int futexWait(atomic_int *address, int expected) {
    return (int)syscall(SYS_futex, (int *)address, FUTEX_WAIT, expected, NULL,
                        NULL, 0);
}

static int futexWake(atomic_int *address, int count) {
    return (int)syscall(SYS_futex, (int *)address, FUTEX_WAKE, count, NULL,
                        NULL, 0);
}

static void futexChildLoop(struct FutexShared *shared) {
    for (;;) {
        if (atomic_load_explicit(&shared->stop, memory_order_acquire) != 0) {
            _exit(0);
        }
        while (atomic_load_explicit(&shared->request, memory_order_acquire) ==
               0) {
            if (atomic_load_explicit(&shared->stop, memory_order_acquire) !=
                0) {
                _exit(0);
            }
            (void)futexWait(&shared->request, 0);
        }
        atomic_store_explicit(&shared->request, 0, memory_order_release);
        atomic_store_explicit(&shared->response, 1, memory_order_release);
        (void)futexWake(&shared->response, 1);
    }
}

static struct FutexShared *futexSharedCreate(void) {
    struct FutexShared *shared =
        mmap(NULL, sizeof(*shared), PROT_READ | PROT_WRITE,
             MAP_SHARED | MAP_ANONYMOUS, -1, 0);

    if (shared == MAP_FAILED) {
        return NULL;
    }
    atomic_init(&shared->request, 0);
    atomic_init(&shared->response, 0);
    atomic_init(&shared->stop, 0);
    return shared;
}

static void futexSharedDestroy(struct FutexShared *shared) {
    if (shared != NULL) {
        munmap(shared, sizeof(*shared));
    }
}

static void futexStopChild(struct FutexShared *shared, pid_t childPid) {
    if (shared != NULL) {
        atomic_store_explicit(&shared->stop, 1, memory_order_release);
        atomic_store_explicit(&shared->request, 1, memory_order_release);
        (void)futexWake(&shared->request, 1);
    }
    if (childPid > 0) {
        kill(childPid, SIGTERM);
        waitpid(childPid, NULL, 0);
    }
}

#endif
