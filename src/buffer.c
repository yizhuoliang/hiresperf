/*
    This per-core ring buffer is for single-producer single-consumer case
*/

#include <linux/slab.h>
#include <linux/sched.h>

#include "buffer.h"
#include "config.h"

inline __attribute__((always_inline)) bool is_full(const HrperfRingBuffer *rb) {
    return ((rb->tail + 1) % HRP_PMC_BUFFER_SIZE) == rb->head;
}

inline __attribute__((always_inline)) void init_ring_buffer(HrperfRingBuffer *rb) {
    rb->head = 0;
    rb->tail = 0;
}

inline __attribute__((always_inline)) void enqueue(HrperfRingBuffer *rb, HrperfTick data) {
    if (!is_full(rb)) {
        rb->buffer[rb->tail] = data;
        rb->tail = (rb->tail + 1) % HRP_PMC_BUFFER_SIZE;
    }
}
