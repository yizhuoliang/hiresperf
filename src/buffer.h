#ifndef BUFFER_H
#define BUFFER_H

#include <linux/types.h>
#include <linux/ktime.h>

#include "config.h"

typedef struct {
#ifdef HRP_USE_TSC
    u64 kts;
#else
    ktime_t kts;
#endif
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

bool is_full(const HrperfRingBuffer *rb);
void init_ring_buffer(HrperfRingBuffer *rb);
void enqueue(HrperfRingBuffer *rb, HrperfLogEntry data);

#endif // BUFFER_H
