#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

#define FIB_DEV "/dev/fibonacci"
#define KTIME_ENABLE "/sys/class/fibonacci/fibonacci/ktime_measure"

static inline long long elapsed(struct timespec *t1, struct timespec *t2)
{
    return (long long) (t2->tv_sec - t1->tv_sec) * 1e9 +
           (long long) (t2->tv_nsec - t1->tv_nsec);
}

int main()
{
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


    for (int i = 0; i <= offset; i++) {
        lseek(fd, i, SEEK_SET);
        clock_gettime(CLOCK_MONOTONIC, &t1);
        read(fd, buf, 1);
        clock_gettime(CLOCK_MONOTONIC, &t2);
        printf("%d %lld\n", i, elapsed(&t1, &t2));
    }

    close(fd);
    return err;

close_fib:
    close(fd);
    return err;
}
