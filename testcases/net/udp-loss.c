/* SPDX-License-Identifier: MPL-2.0 */

#include "lkbench.h"
#include "net_util.h"

#include <sys/socket.h>
#include <unistd.h>

#define BURST_COUNT 64U
#define DATAGRAM_SIZE 1400U
#define RECEIVE_BUFFER (64 * 1024)

static int udpFd = -1;
static struct sockaddr_in address;
static uint64_t totalSent;
static uint64_t totalLost;
static volatile long sink;

static void resetCounters(void) {
    totalSent = 0;
    totalLost = 0;
}

static double sentMetric(const struct LkSamples *samples) {
    (void)samples;
    return (double)totalSent;
}

static double lostMetric(const struct LkSamples *samples) {
    (void)samples;
    return (double)totalLost;
}

static double lossMetric(const struct LkSamples *samples) {
    (void)samples;
    if (totalSent == 0) {
        return 0.0;
    }
    return (double)totalLost * 100.0 / (double)totalSent;
}

static int start(void) {
    uint16_t port;

    udpFd = netUdpBind(&port);
    if (udpFd < 0) {
        return -1;
    }
    if (setsockopt(udpFd, SOL_SOCKET, SO_RCVBUF, &(int){RECEIVE_BUFFER},
                   sizeof(int)) != 0) {
        return -1;
    }
    if (netSetNonblock(udpFd) != 0) {
        return -1;
    }

    address = netUdpAddress(port);
    return 0;
}

static void test(void) {
    char buffer[DATAGRAM_SIZE] = {0};
    uint64_t sent = 0;
    uint64_t received = 0;

    for (unsigned int i = 0; i < BURST_COUNT; i++) {
        if (sendto(udpFd, buffer, sizeof(buffer), 0,
                   (const struct sockaddr *)&address,
                   sizeof(address)) == (ssize_t)sizeof(buffer)) {
            sent++;
        }
    }
    while (recvfrom(udpFd, buffer, sizeof(buffer), 0, NULL, NULL) >= 0) {
        received++;
    }

    totalSent += sent;
    totalLost += sent - received;
    sink = (long)(sent - received);
}

static void terminate(void) {
    if (udpFd >= 0) {
        close(udpFd);
    }
}

LK_METRICS(LK_METRIC("sent", "packets", sentMetric),
           LK_METRIC("lost", "packets", lostMetric),
           LK_METRIC("loss", "%", lossMetric))
LK_TEST_WITH_RESET(resetCounters)
