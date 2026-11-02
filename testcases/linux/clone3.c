/* SPDX-License-Identifier: MPL-2.0 */

#include "lkbench.h"

#include <errno.h>
#include <linux/sched.h>
#include <signal.h>
#include <sys/syscall.h>
#include <sys/wait.h>
#include <unistd.h>

static volatile long sink;

static int probe(void) {
#ifdef SYS_clone3
    long result = syscall(SYS_clone3, NULL, 0);

    if (result < 0 && (errno == ENOSYS || errno == EPERM)) {
        return -1;
    }
    return 0;
#else
    return -1;
#endif
}

static int start(void) { return 0; }

static void test(void) {
#ifdef SYS_clone3
    struct clone_args arguments = {0};
    long pid;

    arguments.exit_signal = SIGCHLD;
    pid = syscall(SYS_clone3, &arguments, sizeof(arguments));
    if (pid == 0) {
        _exit(0);
    }
    if (pid > 0) {
        int status;

        sink = (long)waitpid((pid_t)pid, &status, 0);
    }
#endif
}

static void terminate(void) {}

LK_METRICS_LATENCY()
LK_TEST_OPTIONAL(probe)
