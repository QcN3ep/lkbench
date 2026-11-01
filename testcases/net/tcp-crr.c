/* SPDX-License-Identifier: MPL-2.0 */

#include "lkbench.h"
#include "net_util.h"

#include <signal.h>
#include <stdlib.h>
#include <sys/wait.h>
#include <unistd.h>

static int listenFd = -1;
static pid_t childPid = -1;
static uint16_t port;
static struct sigaction oldSigpipe;
static int sigpipeSaved;
static volatile long sink;

static int start(void) {
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
        for (;;) {
            int connection = accept(listenFd, NULL, NULL);
            char byte;
            ssize_t received;

            if (connection < 0) {
                _exit(0);
            }
            received = recv(connection, &byte, 1, 0);
            if (received == 1) {
                (void)!send(connection, &byte, 1, 0);
            }
            close(connection);
        }
    }

    return 0;
}

static void test(void) {
    int clientFd = netTcpConnect(port);
    char byte = 0;

    if (clientFd < 0) {
        return;
    }
    if (netSetTimeouts(clientFd, 1, 0) != 0) {
        close(clientFd);
        return;
    }
    if (send(clientFd, &byte, 1, 0) == 1) {
        sink = (long)recv(clientFd, &byte, 1, 0);
    }
    close(clientFd);
}

static void terminate(void) {
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
