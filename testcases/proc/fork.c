/* SPDX-License-Identifier: MPL-2.0 */

#include "lkbench.h"

#include <sys/wait.h>
#include <unistd.h>

static volatile long sink;

static int start(void) { return 0; }

static void test(void) {
    pid_t pid = fork();

    if (pid == 0) {
        _exit(0);
    }
    if (pid > 0) {
        int status;

        sink = (long)waitpid(pid, &status, 0);
    }
}

static void terminate(void) {}

LK_METRICS_LATENCY()
LK_TEST()
