#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <unistd.h>

#define FIB_DEV "/dev/fibonacci"
#define KTIME_ENABLE "/sys/class/fibonacci/fibonacci/ktime_measure"

int main()
{
    int err = 0;
    char buf[1];
    int offset = 100;

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

    write(fd_kt, "1", 2);

    for (int i = 0; i <= offset; i++) {
        lseek(fd, i, SEEK_SET);
        read(fd, buf, 1);
        ssize_t kt = write(fd, NULL, 0);
        printf("%d %ld\n", i, kt);
    }

    close(fd_kt);
    close(fd);
    return err;

close_fib:
    close(fd);
    return err;
}
