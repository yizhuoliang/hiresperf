#ifndef _HRPERF_API_H
#define _HRPERF_API_H

#include <stdint.h>
#include <stdio.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <unistd.h>

#define u64 uint64_t

#define HRP_PMC_IOC_MAGIC 'k'
#define HRP_PMC_IOC_START           _IO(HRP_PMC_IOC_MAGIC, 1)
#define HRP_IOC_STOP                _IO(HRP_PMC_IOC_MAGIC, 2)
#define HRP_PMC_IOC_INSTRUCTED_LOG  _IO(HRP_PMC_IOC_MAGIC, 3)
#define HRP_PMC_IOC_TSC_FREQ        _IOR(HRP_PMC_IOC_MAGIC, 10, u64)

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

/*
 * Get the TSC frequency from the kernel module.
 * The unit is cycles per microsecond.
*/
u64 hrperf_get_tsc_freq() {

    int fd;
    u64 tsc_freq;

    // Open the device file
    fd = open("/dev/hrperf_device", O_RDWR);
    if (fd < 0) {
        perror("open");
        return 0;
    }

    // Get the TSC frequency
    if (ioctl(fd, HRP_PMC_IOC_TSC_FREQ, &tsc_freq) < 0) {
        perror("ioctl");
        close(fd);
        return 0;
    }

    close(fd);
    return tsc_freq;
}

/*
 * Instruct the hiresperf to perform exactly one poll and log operation.
*/
void hrperf_instruct_log() {
    int fd;

    // Open the device file
    fd = open("/dev/hrperf_device", O_RDWR);
    if (fd < 0) {
        perror("open");
    }

    // Get the TSC frequency
    if (ioctl(fd, HRP_PMC_IOC_INSTRUCTED_LOG) < 0) {
        perror("ioctl");
        close(fd);
    }
    close(fd);
}

// for the bpf component
int hrp_bpf_start();
void hrp_bpf_stop();

#endif // _HRPERF_API_H
