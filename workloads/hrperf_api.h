#ifndef _HRPERF_API_H
#define _HRPERF_API_H

#include <stdint.h>
#include <stdio.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <unistd.h>

#define u64 uint64_t

#define HRP_PMC_IOC_MAGIC 'k'
#define HRP_PMC_IOC_START                   _IO(HRP_PMC_IOC_MAGIC, 1)
#define HRP_IOC_STOP                        _IO(HRP_PMC_IOC_MAGIC, 2)
#define HRP_PMC_IOC_INSTRUCTED_POLL         _IO(HRP_PMC_IOC_MAGIC, 3)
#define HRP_PMC_IOC_INSTRUCTED_LOG          _IO(HRP_PMC_IOC_MAGIC, 4)
#define HRP_PMC_IOC_INSTRUCTED_POLL_AND_LOG _IO(HRP_PMC_IOC_MAGIC, 5)
#define HRP_PMC_IOC_TSC_FREQ                _IOR(HRP_PMC_IOC_MAGIC, 10, u64)

const char *HRP_PMC_DEVICE_NAME = "/dev/hrperf_device";

static inline __attribute__((always_inline)) int open_hrperf() {
    return open(HRP_PMC_DEVICE_NAME, O_RDWR);
}

static inline __attribute__((always_inline)) int hrperf_ioctl(int fd, unsigned long request, void *arg) {
    return ioctl(fd, request, arg);
}

static inline int hrperf_start() {
    int fd = open_hrperf();

    if (fd < 0) {
        perror("open");
        return 1;
    }

    // Send the start command
    if (hrperf_ioctl(fd, HRP_PMC_IOC_START, NULL) < 0) {
        perror("ioctl");
        close(fd);
        return 1;
    }

    close(fd);
    return 0;
}

static inline int hrperf_pause() {
    int fd = open_hrperf();

    if (fd < 0) {
        perror("open");
        return 1;
    }

    // Send the stop command
    if (hrperf_ioctl(fd, HRP_IOC_STOP, NULL) < 0) {
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
static inline u64 hrperf_get_tsc_freq() {

    int fd = open_hrperf();
    u64 tsc_freq;

    if (fd < 0) {
        perror("open");
        return 0;
    }

    // Get the TSC frequency
    if (hrperf_ioctl(fd, HRP_PMC_IOC_TSC_FREQ, &tsc_freq) < 0) {
        perror("ioctl");
        close(fd);
        return 0;
    }

    close(fd);
    return tsc_freq;
}

/*
 * Instruct the hiresperf to perform exactly one log operation.
*/
static inline void hrperf_instruct_log() {
    int fd = open_hrperf();

    if (fd < 0) {
        perror("open");
    }

    if (hrperf_ioctl(fd, HRP_PMC_IOC_INSTRUCTED_LOG, NULL) < 0) {
        perror("ioctl");
        close(fd);
    }
    close(fd);
}

/*
 * Instruct the hiresperf to perform exactly one poll operation.
*/
static inline void hrperf_instruct_poll() {
    int fd = open_hrperf();
    if (fd < 0) {
        perror("open");
    }
    if (hrperf_ioctl(fd, HRP_PMC_IOC_INSTRUCTED_POLL, NULL) < 0) {
        perror("ioctl");
        close(fd);
    }
    close(fd);
}

/*
 * Instruct the hiresperf to perform exactly one poll and log operation.
*/
static inline void hrperf_instruct_poll_and_log() {
    int fd = open_hrperf();
    if (fd < 0) {
        perror("open");
    }
    if (hrperf_ioctl(fd, HRP_PMC_IOC_INSTRUCTED_POLL_AND_LOG, NULL) < 0) {
        perror("ioctl");
        close(fd);
    }
    close(fd);
}

// for the bpf component
int hrp_bpf_start();
void hrp_bpf_stop();

#endif // _HRPERF_API_H
