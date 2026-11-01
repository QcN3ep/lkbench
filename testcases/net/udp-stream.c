/* SPDX-License-Identifier: MPL-2.0 */

#include "lkbench.h"
#include "net_util.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

#define DATAGRAM_SIZE 1400U
#define RECEIVE_BUFFER 2048U

struct UdpReport {
    uint64_t received;
    double jitterUs;
};

static int receiveFd = -1;
static int reportFd = -1;
static pid_t childPid = -1;
static struct sockaddr_in address;
static uint64_t sentCount;
static uint64_t receivedCount;
static double jitterUs;
static volatile long sink;

static void resetCounters(void) {
    char reset = 0;

    sentCount = 0;
    (void)sendto(receiveFd, &reset, 1, 0, (const struct sockaddr *)&address,
                 sizeof(address));
}

static double throughputMetric(const struct LkSamples *samples) {
    if (samples->elapsedNs == 0) {
        return 0.0;
    }
    return (double)sentCount * (double)DATAGRAM_SIZE * 1000000000.0 /
           ((double)samples->elapsedNs * 1024.0 * 1024.0);
}

static double lossMetric(const struct LkSamples *samples) {
    (void)samples;
    if (sentCount == 0) {
        return 0.0;
    }
    return (double)(sentCount - receivedCount) * 100.0 / (double)sentCount;
}

static double jitterMetric(const struct LkSamples *samples) {
    (void)samples;
    return jitterUs;
}

static int start(void) {
    uint16_t port;
    int pipeFds[2];
    pid_t pid;

    receiveFd = netUdpBind(&port);
    if (receiveFd < 0) {
        return -1;
    }
    address = netUdpAddress(port);
    if (netSetTimeouts(receiveFd, 1, 0) != 0) {
        return -1;
    }
    if (pipe(pipeFds) != 0) {
        return -1;
    }

    pid = fork();
    if (pid < 0) {
        return -1;
    }
    if (pid == 0) {
        char buffer[RECEIVE_BUFFER];
        uint64_t received = 0;
        uint64_t lastArrival = 0;
        uint64_t lastGap = 0;
        uint64_t gapCount = 0;
        double jitterSum = 0.0;
        struct UdpReport report;

        close(pipeFds[0]);
        for (;;) {
            ssize_t result =
                recvfrom(receiveFd, buffer, sizeof(buffer), 0, NULL, NULL);
            uint64_t now;

            if (result == 0) {
                break;
            }
            if (result < 0) {
                if (errno == EINTR || errno == EAGAIN || errno == EWOULDBLOCK) {
                    continue;
                }
                break;
            }
            if (result == 1) {
                received = 0;
                lastArrival = 0;
                lastGap = 0;
                gapCount = 0;
                jitterSum = 0.0;
                continue;
            }

            received++;
            now = netNowNs();
            if (lastArrival != 0) {
                uint64_t gap = now - lastArrival;

                if (lastGap != 0) {
                    uint64_t diff =
                        gap > lastGap ? gap - lastGap : lastGap - gap;
                    jitterSum += (double)diff;
                    gapCount++;
                }
                lastGap = gap;
            }
            lastArrival = now;
        }

        report.received = received;
        report.jitterUs =
            gapCount == 0 ? 0.0 : (jitterSum / (double)gapCount) / 1000.0;
        (void)!write(pipeFds[1], &report, sizeof(report));
        _exit(0);
    }

    close(pipeFds[1]);
    reportFd = pipeFds[0];
    childPid = pid;
    return 0;
}

static void test(void) {
    char buffer[DATAGRAM_SIZE] = {0};

    if (sendto(receiveFd, buffer, sizeof(buffer), 0,
               (const struct sockaddr *)&address,
               sizeof(address)) == (ssize_t)sizeof(buffer)) {
        sentCount++;
    }
    sink = (long)sizeof(buffer);
}

static void terminate(void) {
    char stop = 0;
    struct UdpReport report;

    if (receiveFd >= 0) {
        (void)sendto(receiveFd, &stop, 0, 0, (const struct sockaddr *)&address,
                     sizeof(address));
    }
    if (childPid > 0) {
        waitpid(childPid, NULL, 0);
    }
    if (reportFd >= 0) {
        if (read(reportFd, &report, sizeof(report)) ==
            (ssize_t)sizeof(report)) {
            receivedCount = report.received;
            jitterUs = report.jitterUs;
        }
        close(reportFd);
    }
    if (receiveFd >= 0) {
        close(receiveFd);
    }
}

LK_METRICS(LK_METRICS_LATENCY_LIST,
           LK_METRIC("throughput", "MB/s", throughputMetric),
           LK_METRIC("loss", "%", lossMetric),
           LK_METRIC("jitter", "us", jitterMetric))
LK_TEST_WITH_RESET(resetCounters)
