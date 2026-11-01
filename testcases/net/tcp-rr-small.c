/* SPDX-License-Identifier: MPL-2.0 */

#include "lkbench.h"
#include "net_util.h"

#include <signal.h>
#include <stdlib.h>
#include <sys/wait.h>
#include <unistd.h>

static int listenFd = -1;
static int clientFd = -1;
static pid_t childPid = -1;
static struct sigaction oldSigpipe;
static int sigpipeSaved;
static volatile long sink;

static int start(void) {
    uint16_t port;

    if (netIgnoreSigpipe(&oldSigpipe) != 0) {
        return -1;
    }
    sigpipeSaved = 1;
    listenFd = netTcpListener(&port);
    if (listenFd < 0) {
        return -1;
    }

    childPid = fork();
    if (childPid < 0) {
        return -1;
    }
    if (childPid == 0) {
        int connection = accept(listenFd, NULL, NULL);

        if (connection < 0) {
            _exit(1);
        }
        for (;;) {
            char buffer[64];
            ssize_t received = recv(connection, buffer, sizeof(buffer), 0);

            if (received <= 0) {
                break;
            }
            if (send(connection, buffer, (size_t)received, 0) < 0) {
                break;
            }
        }
        close(connection);
        _exit(0);
    }

    clientFd = netTcpConnect(port);
    if (clientFd < 0) {
        return -1;
    }
    return netSetTimeouts(clientFd, 1, 0) == 0 ? 0 : -1;
}

static void test(void) {
    char request[64] = {0};
    char reply[64];

    if (send(clientFd, request, sizeof(request), 0) ==
        (ssize_t)sizeof(request)) {
        sink = (long)recv(clientFd, reply, sizeof(reply), 0);
    }
}

static void terminate(void) {
    if (clientFd >= 0) {
        close(clientFd);
    }
    if (listenFd >= 0) {
        close(listenFd);
    }
    if (childPid > 0) {
        kill(childPid, SIGTERM);
        waitpid(childPid, NULL, 0);
    }
    if (sigpipeSaved != 0) {
        netRestoreSigpipe(&oldSigpipe);
    }
}

LK_METRICS_LATENCY()
LK_TEST()
