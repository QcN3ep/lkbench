/* SPDX-License-Identifier: MPL-2.0 */

#include "lkbench.h"

#include <signal.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

#define CHUNK_SIZE (64U * 1024U)

static int writeFd = -1;
static pid_t childPid = -1;
static struct sigaction oldSigpipe;
static int sigpipeSaved;
static uint64_t totalBytes;
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
    int fds[2];
    struct sigaction action;

    if (pipe(fds) != 0) {
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
        char buffer[CHUNK_SIZE];

        close(fds[1]);
        while (read(fds[0], buffer, sizeof(buffer)) > 0) {
        }
        _exit(0);
    }

    close(fds[0]);
    writeFd = fds[1];
    return 0;
}

static void test(void) {
    char buffer[CHUNK_SIZE] = {0};
    size_t sent = 0;

    while (sent < sizeof(buffer)) {
        ssize_t result = write(writeFd, buffer + sent, sizeof(buffer) - sent);

        if (result <= 0) {
            break;
        }
        sent += (size_t)result;
    }
    totalBytes += sent;
    sink = (long)sent;
}

static void terminate(void) {
    if (writeFd >= 0) {
        close(writeFd);
    }
    if (childPid > 0) {
        kill(childPid, SIGTERM);
        waitpid(childPid, NULL, 0);
    }
    if (sigpipeSaved != 0) {
        (void)sigaction(SIGPIPE, &oldSigpipe, NULL);
    }
}

LK_METRICS(LK_METRICS_LATENCY_LIST,
           LK_METRIC("throughput", "MB/s", throughputMetric))
LK_TEST_WITH_RESET(resetCounters)
