/*
    This per-core ring buffer is for single-producer single-consumer case
*/

#include <linux/slab.h>
#include <linux/sched.h>
#include <linux/compiler.h>
#include <linux/smp.h>
#include <linux/mm.h>
#include <linux/vmalloc.h>

#include "buffer.h"
#include "config.h"

#if HRP_EXLARGE_HEAP_ALLOCATED_RB
static struct page **buffer_pages;
static size_t buffer_num_pages = 0;

static void free_alloc_pages(void) {
    if (buffer_pages) {
        for (size_t i = 0; i < buffer_num_pages; ++i) {
            if (buffer_pages[i]) {
                __free_page(buffer_pages[i]);
            }
        }
        kfree(buffer_pages);
        buffer_pages = NULL;
    }
}
#endif

static int hrp_alloc_rb_buf(HrperfRingBuffer *rb) {
    HrperfLogEntry *buf = NULL;
    size_t buf_size = HRP_PMC_BUFFER_SIZE * sizeof(HrperfLogEntry);
#if HRP_EXLARGE_HEAP_ALLOCATED_RB
    unsigned long page_aligned_buf_size = PAGE_ALIGN(buf_size);
    buffer_num_pages = page_aligned_buf_size / PAGE_SIZE;
    buffer_pages = kcalloc(buffer_num_pages, sizeof(struct page *), GFP_KERNEL);
    if (!buffer_pages) {
        pr_err("hrperf: Failed to allocate page pointer array\n");
        free_alloc_pages();
        return -ENOMEM;
    }

    for (size_t i = 0; i < buffer_num_pages; ++i) {
        buffer_pages[i] = alloc_page(GFP_KERNEL | __GFP_ZERO);
        if (!buffer_pages[i]) {
            pr_err("hrperf: Failed to allocate page %zu\n", i);
            free_alloc_pages();
            return -ENOMEM;
        }
    }
    
    // We need a contiguous kernel virtual mapping of potentially non-contiguous
    buf = vmap(buffer_pages, buffer_num_pages, VM_MAP, PAGE_KERNEL);
    if (!buf) {
        pr_err("hrperf: Failed to vmap page array\n");
        free_alloc_pages();
        return -ENOMEM;
    }
#else
    buf = kmalloc(buf_size, GFP_KERNEL | __GFP_ZERO);
    if (!buf) {
        pr_err("hrperf: Failed to allocate buffer of size %zu for rb\n", buf_size);
        return -ENOMEM;
    }
#endif
    rb->buffer = buf;
    return 0;
}

inline __attribute__((always_inline)) bool is_full(const HrperfRingBuffer *rb) {
    return ((rb->tail + 1) % HRP_PMC_BUFFER_SIZE) == rb->head;
}

inline __attribute__((always_inline)) int init_ring_buffer(HrperfRingBuffer *rb) {
    rb->head = 0;
    rb->tail = 0;
#if HRP_HEAP_ALLOCATED_RB
    int r = hrp_alloc_rb_buf(rb);
    return r;
#else
    return 0;
#endif
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
