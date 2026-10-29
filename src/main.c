#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#define BATCH_TARGET_NS (50LL * 1000 * 1000)
#define MAX_BATCH_ITERS 16777216UL
#define SAMPLE_COUNT 5

static volatile unsigned long sink;

static int nowNs(int64_t *ns) {
    struct timespec ts;

    if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0) {
        fprintf(stderr,
                "lkbench: clock_gettime(CLOCK_MONOTONIC) failed (errno %d)\n",
                errno);
        return -1;
    }

    *ns = ((int64_t)ts.tv_sec * 1000000000LL) + (int64_t)ts.tv_nsec;
    return 0;
}

static void runSyscall(unsigned long iterations) {
    for (unsigned long i = 0; i < iterations; i++) {
        sink += (unsigned long)getppid();
    }
}

static int measureBatch(unsigned long iterations, int64_t *elapsedNs) {
    int64_t start;
    int64_t end;

    if (nowNs(&start) != 0) {
        return -1;
    }
    runSyscall(iterations);
    if (nowNs(&end) != 0) {
        return -1;
    }

    *elapsedNs = end - start;
    return 0;
}

static int measureSyscall(double *minNs, double *medianNs, int *samples) {
    unsigned long iterations = 1000;
    int64_t elapsedNs;
    double values[SAMPLE_COUNT];

    for (;;) {
        if (measureBatch(iterations, &elapsedNs) != 0) {
            return -1;
        }
        if (elapsedNs >= BATCH_TARGET_NS || iterations >= MAX_BATCH_ITERS) {
            break;
        }
        iterations *= 2;
    }

    /* Discard one calibrated batch as warm-up. */
    if (measureBatch(iterations, &elapsedNs) != 0) {
        return -1;
    }

    for (int i = 0; i < SAMPLE_COUNT; i++) {
        if (measureBatch(iterations, &elapsedNs) != 0) {
            return -1;
        }
        values[i] = (double)elapsedNs / (double)iterations;
    }

    for (int i = 1; i < SAMPLE_COUNT; i++) {
        double value = values[i];
        int j = i - 1;

        while (j >= 0 && values[j] > value) {
            values[j + 1] = values[j];
            j--;
        }
        values[j + 1] = value;
    }

    *minNs = values[0];
    *medianNs = values[SAMPLE_COUNT / 2];
    *samples = SAMPLE_COUNT;
    return 0;
}

static void printUsage(FILE *stream, const char *program) {
    fprintf(stream, "Usage: %s [--help]\n", program);
    fprintf(stream, "Run the built-in syscall benchmark (default).\n");
}

int main(int argc, char **argv) {
    const char *program = argc > 0 ? argv[0] : "lkbench";
    double minNs;
    double medianNs;
    int samples;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--help") == 0) {
            printUsage(stdout, program);
            return 0;
        }

        fprintf(stderr, "lkbench: unknown argument: %s\n", argv[i]);
        printUsage(stderr, program);
        return 2;
    }

    if (measureSyscall(&minNs, &medianNs, &samples) != 0) {
        return 1;
    }

    printf("syscall  min %.1f ns  median %.1f ns  (%d samples)\n", minNs,
           medianNs, samples);
    return 0;
}
