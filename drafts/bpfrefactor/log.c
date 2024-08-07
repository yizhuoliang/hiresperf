#include <stdatomic.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>
#include <string.h>
#include <stdio.h>

#include "hrp_bpf.h"
#include "log.h"

volatile atomic_size_t log_offset; // Track the next available offset

// Initialize and mmap the log file
void* hrp_bpf_log_init() {
    int fd = open(HRP_BPF_LOG_FILE_PATH, O_RDWR | O_CREAT, 0666);
    if (fd < 0) {
        perror("Failed to open log file");
        return NULL;
    }

    // Resize the file to the specified size
    if (ftruncate(fd, HRP_BPF_LOG_FILE_SIZE) == -1) {
        perror("Failed to resize log file");
        close(fd);
        return NULL;
    }

    // Memory map the log file
    void *mapped = mmap(NULL, HRP_BPF_LOG_FILE_SIZE, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (mapped == MAP_FAILED) {
        perror("Failed to mmap log file");
        close(fd);
        return NULL;
    }

    close(fd); // The file descriptor is no longer needed after mmap
    return mapped;
}

// The callback function for handling BPF events
int hrp_bpf_event_callback(void *ctx, void *data, size_t size) {
    struct hrp_bpf_event *e = (struct hrp_bpf_event *)data;
    size_t offset = atomic_fetch_add_explicit(&log_offset, sizeof(struct hrp_bpf_event), memory_order_relaxed);

    if (offset + sizeof(struct hrp_bpf_event) > HRP_BPF_LOG_FILE_SIZE) {
        printf("Log file is full\n");
        return 0;
    }

    struct hrp_bpf_event *log_ptr = (struct hrp_bpf_event *)((char *)ctx + offset);
    memcpy(log_ptr, e, sizeof(struct hrp_bpf_event));
    return 0;
}
