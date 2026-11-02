/* SPDX-License-Identifier: MPL-2.0 */

#include "lkbench.h"

#include <spawn.h>
#include <sys/wait.h>
#include <unistd.h>

extern char **environ;

static volatile long sink;

static int start(void) { return 0; }

static void test(void) {
    char *arguments[] = {(char *)"true", NULL};
    pid_t pid;

    if (posix_spawnp(&pid, "true", NULL, NULL, arguments, environ) == 0) {
        int status;

        sink = (long)waitpid(pid, &status, 0);
    }
}

static void terminate(void) {}

LK_METRICS_LATENCY()
LK_TEST()
