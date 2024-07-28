#ifndef _HRP_BUFFER_H
#define _HRP_BUFFER_H

#include <linux/kernel.h>
#include <linux/ktime.h>
#include "config.h"

typedef struct {
    // unsigned long long tsc;
    ktime_t kts;
    unsigned long long cpu_unhalt;
    unsigned long long llc_misses;
    unsigned long long sw_prefetch;
} HrperfTick;

typedef struct {
    HrperfTick buffer[HRP_BUFFER_SIZE];
    size_t head;
    size_t tail;
} HrperfRingBuffer;

bool is_full(const HrperfRingBuffer *rb);
void init_ring_buffer(HrperfRingBuffer *rb);
void enqueue(HrperfRingBuffer *rb, HrperfTick data);
void print_and_clear(HrperfRingBuffer *rb);

#endif
