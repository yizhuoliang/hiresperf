#ifndef _HRPERF_API_H
#define _HRPERF_API_H

#include <stdio.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <unistd.h>
#include <stdint.h>

#define HRP_PMC_IOC_MAGIC 'k'
#define HRP_PMC_IOC_START _IO(HRP_PMC_IOC_MAGIC, 1)
#define HRP_IOC_STOP _IO(HRP_PMC_IOC_MAGIC, 2)

// Shared buffer ioctl commands
#define HRP_PMC_IOC_SHARED_INIT    _IOW(HRP_PMC_IOC_MAGIC, 3, unsigned long)
#define HRP_PMC_IOC_SHARED_START   _IO(HRP_PMC_IOC_MAGIC, 4)
#define HRP_PMC_IOC_SHARED_PAUSE   _IO(HRP_PMC_IOC_MAGIC, 5)
#define HRP_PMC_IOC_SHARED_INFO    _IOR(HRP_PMC_IOC_MAGIC, 6, struct shared_buffer_info)
#define HRP_PMC_IOC_SHARED_CPU_INFO _IOWR(HRP_PMC_IOC_MAGIC, 7, struct shared_cpu_buffer_info)

// Shared buffer info structures
struct shared_buffer_info {
    uint32_t cpu_count;
    uint32_t buffer_size;
    uint32_t entry_size;
};

struct shared_cpu_buffer_info {
    uint32_t cpu_id;
    uint64_t phys_addr;
    uint32_t buffer_size;
};

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

// Shared buffer API functions
int hrperf_init_shared_buffers(size_t per_buffer_size);
int hrperf_start_shared_buffers(void);
int hrperf_pause_shared_buffers(void);
int hrperf_get_shared_buffer_info(struct shared_buffer_info *info);
int hrperf_get_cpu_buffer_info(int cpu_id, struct shared_cpu_buffer_info *info);
void* hrperf_map_cpu_buffer(int cpu_id);
void hrperf_unmap_buffer(void *buffer, size_t size);

// for the bpf component
int hrp_bpf_start();
void hrp_bpf_stop();

#endif // _HRPERF_API_H
