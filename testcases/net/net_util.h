/* SPDX-License-Identifier: MPL-2.0 */

#ifndef LK_NET_UTIL_H
#define LK_NET_UTIL_H

#include <arpa/inet.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <signal.h>
#include <stdint.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <time.h>
#include <unistd.h>

static inline uint64_t netNowNs(void) {
    struct timespec ts;

    if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0) {
        return 0;
    }
    return ((uint64_t)ts.tv_sec * 1000000000ULL) + (uint64_t)ts.tv_nsec;
}

static inline int netTcpListener(uint16_t *port) {
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    struct sockaddr_in address;
    socklen_t addressLength = sizeof(address);

    if (fd < 0) {
        return -1;
    }
    memset(&address, 0, sizeof(address));
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    address.sin_port = 0;

    if (bind(fd, (const struct sockaddr *)&address, sizeof(address)) != 0 ||
        listen(fd, 16) != 0 ||
        getsockname(fd, (struct sockaddr *)&address, &addressLength) != 0) {
        close(fd);
        return -1;
    }

    *port = ntohs(address.sin_port);
    return fd;
}

static inline int netTcpConnect(uint16_t port) {
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    struct sockaddr_in address;

    if (fd < 0) {
        return -1;
    }
    memset(&address, 0, sizeof(address));
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    address.sin_port = htons(port);

    if (connect(fd, (const struct sockaddr *)&address, sizeof(address)) != 0) {
        close(fd);
        return -1;
    }
    return fd;
}

static inline int netUdpBind(uint16_t *port) {
    int fd = socket(AF_INET, SOCK_DGRAM, 0);
    struct sockaddr_in address;
    socklen_t addressLength = sizeof(address);

    if (fd < 0) {
        return -1;
    }
    memset(&address, 0, sizeof(address));
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    address.sin_port = 0;

    if (bind(fd, (const struct sockaddr *)&address, sizeof(address)) != 0 ||
        getsockname(fd, (struct sockaddr *)&address, &addressLength) != 0) {
        close(fd);
        return -1;
    }

    *port = ntohs(address.sin_port);
    return fd;
}

static inline struct sockaddr_in netUdpAddress(uint16_t port) {
    struct sockaddr_in address;

    memset(&address, 0, sizeof(address));
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    address.sin_port = htons(port);
    return address;
}

static inline int netSetNonblock(int fd) {
    int flags = fcntl(fd, F_GETFL, 0);

    if (flags < 0) {
        return -1;
    }
    return fcntl(fd, F_SETFL, flags | O_NONBLOCK);
}

static inline int netIgnoreSigpipe(struct sigaction *oldAction) {
    struct sigaction action;

    memset(&action, 0, sizeof(action));
    action.sa_handler = SIG_IGN;
    sigemptyset(&action.sa_mask);
    return sigaction(SIGPIPE, &action, oldAction);
}

static inline void netRestoreSigpipe(const struct sigaction *oldAction) {
    if (oldAction != NULL) {
        (void)sigaction(SIGPIPE, oldAction, NULL);
    }
}

static inline int netSetTimeouts(int fd, long seconds, long useconds) {
    struct timeval timeout;

    timeout.tv_sec = seconds;
    timeout.tv_usec = useconds;
    if (setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout)) !=
        0) {
        return -1;
    }
    return setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout));
}

#endif
