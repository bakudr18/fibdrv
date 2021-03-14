#include <assert.h>
#include <fcntl.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>
#include "mlock_check.h"

#define FIB_DEV "/dev/fibonacci"
#define KTIME_ENABLE "/sys/class/fibonacci/fibonacci/ktime_measure"
#define PRE_ALLOCATION_SIZE \
    (40 * 1024 * 1024)  // ulimit -l to check the maxinum size may lock into
                        // memory in process

static inline long long elapsed(struct timespec *t1, struct timespec *t2)
{
    return (long long) (t2->tv_sec - t1->tv_sec) * 1e9 +
           (long long) (t2->tv_nsec - t1->tv_nsec);
}

int main()
{
    configure_malloc_behavior();
    /* malloc and touch generated */
    reserve_process_memory(PRE_ALLOCATION_SIZE);
    check_pagefault(INT_MAX, INT_MAX);
    /* 2nd malloc and use gnenrated */
    reserve_process_memory(PRE_ALLOCATION_SIZE);

    /* Check all pages are loaded to memory */
    assert(check_pagefault(0, 0));

    struct timespec t1, t2;
    char buf[1];
    int offset = 100;
    int err = 0;

    int fd = open(FIB_DEV, O_RDWR);
    if (fd < 0) {
        perror("Failed to open character device");
        exit(1);
    }

    int fd_kt = open(KTIME_ENABLE, O_RDWR);
    if (fd_kt < 0) {
        perror("Failed to open sysfs");
        err = 1;
        goto close_fib;
    }

    write(fd_kt, "0", 2);
    close(fd_kt);

    /* Touch variables which will be used in time critial section. Even
     * if mlockall has been called, page fault still happens possibly.
     * Actually clock_gettime causes minor page fault when first time call
     * here. */
    clock_gettime(CLOCK_MONOTONIC, &t1);
    read(fd, buf, 1);
    clock_gettime(CLOCK_MONOTONIC, &t2);
    check_pagefault(0, 0);

    for (int i = 0; i <= offset; i++) {
        lseek(fd, i, SEEK_SET);
        clock_gettime(CLOCK_MONOTONIC, &t1);
        read(fd, buf, 1);
        clock_gettime(CLOCK_MONOTONIC, &t2);
        assert(check_pagefault(0, 0));
        printf("%d %lld\n", i, elapsed(&t1, &t2));
    }

    close(fd);
    return err;

close_fib:
    close(fd);
    return err;
}
