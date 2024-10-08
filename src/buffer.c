/*
    This per-core ring buffer is for single-producer single-consumer case
*/

#include <linux/slab.h>
#include <linux/sched.h>
#include <linux/compiler.h>
#include <linux/smp.h>

#include "buffer.h"
#include "config.h"

inline __attribute__((always_inline)) bool is_full(const HrperfRingBuffer *rb) {
    return ((rb->tail + 1) % HRP_PMC_BUFFER_SIZE) == rb->head;
}

inline __attribute__((always_inline)) void init_ring_buffer(HrperfRingBuffer *rb) {
    rb->head = 0;
    rb->tail = 0;
}

inline __attribute__((always_inline)) void enqueue(HrperfRingBuffer *rb, HrperfLogEntry data) {
    unsigned int next_tail = (rb->tail + 1) % HRP_PMC_BUFFER_SIZE;

    if (next_tail == rb->head) {
        // buffer is full, data will be lost
        return;
    }

    rb->buffer[rb->tail] = data;
    smp_store_release(&rb->tail, next_tail);
}
