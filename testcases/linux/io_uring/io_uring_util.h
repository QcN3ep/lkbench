/* SPDX-License-Identifier: MPL-2.0 */

#ifndef LKBENCH_IO_URING_UTIL_H
#define LKBENCH_IO_URING_UTIL_H

#include <errno.h>
#include <linux/io_uring.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/syscall.h>
#include <unistd.h>

struct IoUring {
    int fd;
    unsigned *sqHead;
    unsigned *sqTail;
    unsigned *sqMask;
    unsigned *sqEntries;
    unsigned *sqArray;
    struct io_uring_sqe *sqes;
    unsigned *cqHead;
    unsigned *cqTail;
    unsigned *cqMask;
    unsigned *cqEntries;
    struct io_uring_cqe *cqes;
    void *sqRing;
    void *cqRing;
    void *sqeRing;
    size_t sqRingSize;
    size_t cqRingSize;
    size_t sqeRingSize;
};

static int ioUringProbe(void) {
    struct io_uring_params params;
    long fd;

    memset(&params, 0, sizeof(params));
    fd = syscall(SYS_io_uring_setup, 2, &params);
    if (fd < 0) {
        return -1;
    }
    close((int)fd);
    return 0;
}

static int ioUringEnter(struct IoUring *ring, unsigned submit, unsigned wait) {
    int result;

    do {
        result = (int)syscall(SYS_io_uring_enter, ring->fd, submit, wait,
                              IORING_ENTER_GETEVENTS, NULL, 0);
    } while (result < 0 && errno == EINTR);
    return result < 0 ? -1 : 0;
}

static void ioUringDestroy(struct IoUring *ring) {
    if (ring->sqRing != NULL) {
        munmap(ring->sqRing, ring->sqRingSize);
    }
    if (ring->cqRing != NULL) {
        munmap(ring->cqRing, ring->cqRingSize);
    }
    if (ring->sqeRing != NULL) {
        munmap(ring->sqeRing, ring->sqeRingSize);
    }
    if (ring->fd >= 0) {
        close(ring->fd);
    }
    memset(ring, 0, sizeof(*ring));
    ring->fd = -1;
}

static int ioUringInit(struct IoUring *ring, unsigned entries) {
    struct io_uring_params params;

    memset(ring, 0, sizeof(*ring));
    ring->fd = -1;
    memset(&params, 0, sizeof(params));

    ring->fd = (int)syscall(SYS_io_uring_setup, entries, &params);
    if (ring->fd < 0) {
        return -1;
    }

    ring->sqRingSize =
        params.sq_off.array + params.sq_entries * sizeof(unsigned);
    ring->cqRingSize =
        params.cq_off.cqes + params.cq_entries * sizeof(struct io_uring_cqe);
    ring->sqeRingSize = params.sq_entries * sizeof(struct io_uring_sqe);

    ring->sqRing = mmap(NULL, ring->sqRingSize, PROT_READ | PROT_WRITE,
                        MAP_SHARED, ring->fd, IORING_OFF_SQ_RING);
    if (ring->sqRing == MAP_FAILED) {
        ring->sqRing = NULL;
        ioUringDestroy(ring);
        return -1;
    }
    ring->cqRing = mmap(NULL, ring->cqRingSize, PROT_READ | PROT_WRITE,
                        MAP_SHARED, ring->fd, IORING_OFF_CQ_RING);
    if (ring->cqRing == MAP_FAILED) {
        ring->cqRing = NULL;
        ioUringDestroy(ring);
        return -1;
    }
    ring->sqeRing = mmap(NULL, ring->sqeRingSize, PROT_READ | PROT_WRITE,
                         MAP_SHARED, ring->fd, IORING_OFF_SQES);
    if (ring->sqeRing == MAP_FAILED) {
        ring->sqeRing = NULL;
        ioUringDestroy(ring);
        return -1;
    }

    ring->sqHead = (unsigned *)((char *)ring->sqRing + params.sq_off.head);
    ring->sqTail = (unsigned *)((char *)ring->sqRing + params.sq_off.tail);
    ring->sqMask = (unsigned *)((char *)ring->sqRing + params.sq_off.ring_mask);
    ring->sqEntries =
        (unsigned *)((char *)ring->sqRing + params.sq_off.ring_entries);
    ring->sqArray = (unsigned *)((char *)ring->sqRing + params.sq_off.array);
    ring->sqes = ring->sqeRing;
    ring->cqHead = (unsigned *)((char *)ring->cqRing + params.cq_off.head);
    ring->cqTail = (unsigned *)((char *)ring->cqRing + params.cq_off.tail);
    ring->cqMask = (unsigned *)((char *)ring->cqRing + params.cq_off.ring_mask);
    ring->cqEntries =
        (unsigned *)((char *)ring->cqRing + params.cq_off.ring_entries);
    ring->cqes =
        (struct io_uring_cqe *)((char *)ring->cqRing + params.cq_off.cqes);
    return 0;
}

static int ioUringSubmit(struct IoUring *ring, unsigned opcode, int fd,
                         void *buffer, unsigned length, int *result) {
    unsigned tail = __atomic_load_n(ring->sqTail, __ATOMIC_ACQUIRE);
    unsigned index = tail & *ring->sqMask;
    struct io_uring_sqe *sqe = &ring->sqes[index];
    unsigned head;
    unsigned cqTail;

    memset(sqe, 0, sizeof(*sqe));
    sqe->opcode = (uint8_t)opcode;
    sqe->fd = fd;
    sqe->addr = (uint64_t)(uintptr_t)buffer;
    sqe->len = length;
    sqe->user_data = 1;
    ring->sqArray[index] = index;
    __atomic_store_n(ring->sqTail, tail + 1, __ATOMIC_RELEASE);

    if (ioUringEnter(ring, 1, 1) != 0) {
        return -1;
    }

    head = __atomic_load_n(ring->cqHead, __ATOMIC_ACQUIRE);
    while ((cqTail = __atomic_load_n(ring->cqTail, __ATOMIC_ACQUIRE)) == head) {
        if (ioUringEnter(ring, 0, 1) != 0) {
            return -1;
        }
    }

    *result = ring->cqes[head & *ring->cqMask].res;
    __atomic_store_n(ring->cqHead, head + 1, __ATOMIC_RELEASE);
    return 0;
}

#endif
