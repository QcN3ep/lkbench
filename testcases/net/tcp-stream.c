/* SPDX-License-Identifier: MPL-2.0 */

#include "lkbench.h"
#include "net_util.h"

#include <signal.h>
#include <stdlib.h>
#include <sys/wait.h>
#include <unistd.h>

#define CHUNK_SIZE (64U * 1024U)

static int listenFd = -1;
static int clientFd = -1;
static pid_t childPid = -1;
static uint64_t totalBytes;
static struct sigaction oldSigpipe;
static int sigpipeSaved;
static volatile long sink;

static void resetCounters(void) { totalBytes = 0; }

static double throughputMetric(const struct LkSamples *samples) {
    if (samples->elapsedNs == 0) {
        return 0.0;
    }
    return (double)totalBytes * 1000000000.0 /
           ((double)samples->elapsedNs * 1024.0 * 1024.0);
}

static int start(void) {
    uint16_t port;
    char buffer[CHUNK_SIZE] = {0};

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
        while (recv(connection, buffer, sizeof(buffer), 0) > 0) {
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
    char buffer[CHUNK_SIZE] = {0};
    size_t sent = 0;

    while (sent < sizeof(buffer)) {
        ssize_t result =
            send(clientFd, buffer + sent, sizeof(buffer) - sent, 0);

        if (result <= 0) {
            break;
        }
        sent += (size_t)result;
    }
    totalBytes += sent;
    sink = (long)sent;
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

LK_METRICS(LK_METRICS_LATENCY_LIST,
           LK_METRIC("throughput", "MB/s", throughputMetric))
LK_TEST_WITH_RESET(resetCounters)
