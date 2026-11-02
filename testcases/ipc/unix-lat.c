/* SPDX-License-Identifier: MPL-2.0 */

#include "lkbench.h"

#include <signal.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <unistd.h>

static int fds[2] = {-1, -1};
static pid_t childPid = -1;
static struct sigaction oldSigpipe;
static int sigpipeSaved;
static volatile long sink;

static int start(void) {
    struct sigaction action;

    if (socketpair(AF_UNIX, SOCK_STREAM, 0, fds) != 0) {
        return -1;
    }

    memset(&action, 0, sizeof(action));
    action.sa_handler = SIG_IGN;
    sigemptyset(&action.sa_mask);
    if (sigaction(SIGPIPE, &action, &oldSigpipe) != 0) {
        return -1;
    }
    sigpipeSaved = 1;

    childPid = fork();
    if (childPid < 0) {
        return -1;
    }
    if (childPid == 0) {
        for (;;) {
            char byte;
            ssize_t received = recv(fds[1], &byte, 1, 0);

            if (received <= 0) {
                _exit(0);
            }
            if (send(fds[1], &byte, 1, 0) != 1) {
                _exit(1);
            }
        }
    }

    return 0;
}

static void test(void) {
    char byte = 1;
    char reply;

    if (send(fds[0], &byte, 1, 0) == 1) {
        sink = (long)recv(fds[0], &reply, 1, 0);
    }
}

static void terminate(void) {
    if (fds[0] >= 0) {
        close(fds[0]);
    }
    if (fds[1] >= 0) {
        close(fds[1]);
    }
    if (childPid > 0) {
        kill(childPid, SIGTERM);
        waitpid(childPid, NULL, 0);
    }
    if (sigpipeSaved != 0) {
        (void)sigaction(SIGPIPE, &oldSigpipe, NULL);
    }
}

LK_METRICS_LATENCY()
LK_TEST()
