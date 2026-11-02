/* SPDX-License-Identifier: MPL-2.0 */

#include "lkbench.h"

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/syscall.h>
#include <sys/types.h>
#include <unistd.h>

#define COPY_SIZE 4096

static char inputPath[] = "/tmp/lkbench-cfr-in-XXXXXX";
static char outputPath[] = "/tmp/lkbench-cfr-out-XXXXXX";
static int inputFd = -1;
static int outputFd = -1;
static volatile long sink;

static int probe(void) {
#ifdef SYS_copy_file_range
    long result = syscall(SYS_copy_file_range, -1, NULL, -1, NULL, 0, 0);

    if (result < 0 && errno == ENOSYS) {
        return -1;
    }
    return 0;
#else
    return -1;
#endif
}

static int start(void) {
    char buffer[COPY_SIZE] = {0};
    size_t written = 0;

    inputFd = mkstemp(inputPath);
    if (inputFd < 0) {
        return -1;
    }
    outputFd = mkstemp(outputPath);
    if (outputFd < 0) {
        return -1;
    }

    while (written < sizeof(buffer)) {
        ssize_t result =
            write(inputFd, buffer + written, sizeof(buffer) - written);

        if (result <= 0) {
            return -1;
        }
        written += (size_t)result;
    }
    return 0;
}

static void test(void) {
#ifdef SYS_copy_file_range
    loff_t inputOffset = 0;
    loff_t outputOffset = 0;

    sink = syscall(SYS_copy_file_range, inputFd, &inputOffset, outputFd,
                   &outputOffset, COPY_SIZE, 0);
#endif
}

static void terminate(void) {
    if (inputFd >= 0) {
        close(inputFd);
    }
    if (outputFd >= 0) {
        close(outputFd);
    }
    unlink(inputPath);
    unlink(outputPath);
}

LK_METRICS_LATENCY()
LK_TEST_OPTIONAL(probe)
