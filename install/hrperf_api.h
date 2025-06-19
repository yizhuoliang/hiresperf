#ifndef _HRPERF_API_H
#define _HRPERF_API_H

#include <stdio.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <unistd.h>

#define HRP_PMC_IOC_MAGIC 'k'
#define HRP_PMC_IOC_START _IO(HRP_PMC_IOC_MAGIC, 1)
#define HRP_IOC_STOP _IO(HRP_PMC_IOC_MAGIC, 2)

int hrperf_start() {
    int fd;

    // Open the device file
    fd = open("/dev/hrperf_device", O_RDWR);
    if (fd < 0) {
        perror("open");
        return 1;
    }

    // Send the start command
    if (ioctl(fd, HRP_PMC_IOC_START) < 0) {
        perror("ioctl");
        close(fd);
        return 1;
    }

    close(fd);
    return 0;
}

int hrperf_pause() {
    int fd;

    // Open the device file
    fd = open("/dev/hrperf_device", O_RDWR);
    if (fd < 0) {
        perror("open");
        return 1;
    }

    // Send the stop command
    if (ioctl(fd, HRP_IOC_STOP) < 0) {
        perror("ioctl");
        close(fd);
        return 1;
    }

    close(fd);
    return 0;
}

// for the bpf component
int hrp_bpf_start();
void hrp_bpf_stop();

#endif // _HRPERF_API_H
