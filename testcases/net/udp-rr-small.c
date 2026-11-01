/* SPDX-License-Identifier: MPL-2.0 */

#include "lkbench.h"
#include "net_util.h"

#include <unistd.h>

static int udpFd = -1;
static struct sockaddr_in address;
static volatile long sink;

static int start(void) {
    uint16_t port;

    udpFd = netUdpBind(&port);
    if (udpFd < 0) {
        return -1;
    }
    address = netUdpAddress(port);
    return netSetTimeouts(udpFd, 1, 0) == 0 ? 0 : -1;
}

static void test(void) {
    char request[64] = {0};
    char reply[64];

    sink = (long)sendto(udpFd, request, sizeof(request), 0,
                        (const struct sockaddr *)&address, sizeof(address));
    if (sink == (long)sizeof(request)) {
        sink = (long)recvfrom(udpFd, reply, sizeof(reply), 0, NULL, NULL);
    }
}

static void terminate(void) {
    if (udpFd >= 0) {
        close(udpFd);
    }
}

LK_METRICS_LATENCY()
LK_TEST()
