#ifndef BUFFER_H
#define BUFFER_H

#include <linux/types.h>
#include <linux/ktime.h>
#include "config.h"

typedef struct {
    ktime_t kts;
    unsigned long long stall_mem;
    unsigned long long inst_retire;
    unsigned long long cpu_unhalt;
    unsigned long long llc_misses;
    unsigned long long sw_prefetch;
} HrperfTick;

typedef struct {
    int cpu_id;
    HrperfTick tick;
} HrperfLogEntry;

typedef struct {
    HrperfLogEntry buffer[HRP_PMC_BUFFER_SIZE];
    volatile unsigned int head;
    volatile unsigned int tail;
} HrperfRingBuffer;

// Shared memory buffer structure
typedef struct {
    void *kernel_addr;         // Kernel virtual address
    phys_addr_t phys_addr;     // Physical address for userspace
    struct page **pages;       // Pages backing the buffer
    size_t size;               // Total buffer size
    size_t page_count;         // Number of pages
    atomic_t write_idx;        // Write index for producer
} HrperfSharedBuffer;

// Per-CPU shared buffer collection
typedef struct {
    HrperfSharedBuffer *buffers;  // Per-CPU shared buffers
    int cpu_count;                // Number of CPUs/buffers
    bool enabled;                 // Whether shared buffers are active
} HrperfSharedBufferArray;

bool is_full(const HrperfRingBuffer *rb);
void init_ring_buffer(HrperfRingBuffer *rb);
void enqueue(HrperfRingBuffer *rb, HrperfLogEntry data);

// Shared buffer interface
int init_shared_buffer_array(HrperfSharedBufferArray *sbuf_array, size_t per_buffer_size);
void cleanup_shared_buffer_array(HrperfSharedBufferArray *sbuf_array);
int write_to_shared_buffer(HrperfSharedBuffer *sbuf, const HrperfLogEntry *entry);

#endif // BUFFER_H
