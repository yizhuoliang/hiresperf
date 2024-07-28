#include <stdio.h>
#include <fcntl.h>
#include <sys/ioctl.h>

#define HRP_IOC_MAGIC 'k'
#define HRP_IOC_START _IO(HRP_IOC_MAGIC, 1)

int main() {
    int fd;

    // Open the device file
    fd = open("/dev/hrperf_device", O_RDWR);
    if (fd < 0) {
        perror("open");
        return 1;
    }

    // Send the start command
    if (ioctl(fd, HRP_IOC_START) < 0) {
        perror("ioctl");
        close(fd);
        return 1;
    }

    close(fd);
    return 0;
}