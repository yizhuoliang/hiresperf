#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/percpu.h>
#include <linux/slab.h>
#include <linux/kthread.h>
#include <linux/delay.h>
#include <linux/sched.h>

#define POLL_BUFFER_SIZE 20

typedef struct {
    uint64_t buffer[POLL_BUFFER_SIZE];
    size_t head;
    size_t tail;
} RingBuffer;

static inline bool is_full(const RingBuffer *rb) {
    return ((rb->tail + 1) % POLL_BUFFER_SIZE) == rb->head;
}

static void init_ring_buffer(RingBuffer *rb) {
    rb->head = 0;
    rb->tail = 0;
}

static void enqueue(RingBuffer *rb, uint64_t value) {
    if (!is_full(rb)) {
        rb->buffer[rb->tail] = value;
        rb->tail = (rb->tail + 1) % POLL_BUFFER_SIZE;
    }
}

static void print_and_clear(RingBuffer *rb) {
    while (rb->head != rb->tail) {
        printk(KERN_INFO "%llu ", rb->buffer[rb->head]);
        rb->head = (rb->head + 1) % POLL_BUFFER_SIZE;
    }
    printk(KERN_INFO "\n");
}
