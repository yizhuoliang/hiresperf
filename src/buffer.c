/*
    This per-core ring buffer is for single-producer single-consumer case
*/

#include <linux/slab.h>
#include <linux/sched.h>
#include <linux/compiler.h>
#include <linux/smp.h>
#include <linux/vmalloc.h>
#include <linux/mm.h>
#include <linux/atomic.h>

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

/*
 * Initialize array of per-CPU shared buffers for userspace monitoring
 */
int init_shared_buffer_array(HrperfSharedBufferArray *sbuf_array, size_t per_buffer_size)
{
    int i, j, cpu;
    HrperfSharedBuffer *sbuf;
    
    if (!sbuf_array || per_buffer_size < PAGE_SIZE)
        return -EINVAL;
    
    // Get number of online CPUs
    sbuf_array->cpu_count = num_online_cpus();
    
    // Allocate array of per-CPU buffers
    sbuf_array->buffers = kmalloc(sizeof(HrperfSharedBuffer) * sbuf_array->cpu_count, GFP_KERNEL);
    if (!sbuf_array->buffers)
        return -ENOMEM;
    
    // Initialize each CPU's buffer
    for_each_online_cpu(cpu) {
        sbuf = &sbuf_array->buffers[cpu];
        
        // Round up to page size
        sbuf->size = PAGE_ALIGN(per_buffer_size);
        sbuf->page_count = sbuf->size / PAGE_SIZE;
        
        // Allocate page pointer array
        sbuf->pages = kmalloc(sizeof(struct page *) * sbuf->page_count, GFP_KERNEL);
        if (!sbuf->pages)
            goto cleanup;
        
        // Allocate buffer memory
        sbuf->kernel_addr = vmalloc_user(sbuf->size);
        if (!sbuf->kernel_addr) {
            kfree(sbuf->pages);
            sbuf->pages = NULL;
            goto cleanup;
        }
        
        // Store page references
        for (j = 0; j < sbuf->page_count; j++) {
            sbuf->pages[j] = vmalloc_to_page(sbuf->kernel_addr + (j * PAGE_SIZE));
        }
        
        // Get physical address for userspace mapping
        sbuf->phys_addr = page_to_pfn(sbuf->pages[0]) << PAGE_SHIFT;
        
        // Initialize write index
        atomic_set(&sbuf->write_idx, 0);
        
        // Setup buffer header (metadata at start of buffer)
        uint32_t *header = (uint32_t *)sbuf->kernel_addr;
        header[0] = 0xABCD1234;                 // Magic number
        header[1] = cpu;                         // CPU ID
        header[2] = sizeof(HrperfLogEntry);      // Entry size
        header[3] = 0;                           // Write index
    }
    
    return 0;
    
cleanup:
    // Clean up in case of error
    for (i = 0; i < cpu; i++) {
        sbuf = &sbuf_array->buffers[i];
        
        if (sbuf->kernel_addr) {
            vfree(sbuf->kernel_addr);
            sbuf->kernel_addr = NULL;
        }
        
        if (sbuf->pages) {
            kfree(sbuf->pages);
            sbuf->pages = NULL;
        }
    }
    
    kfree(sbuf_array->buffers);
    sbuf_array->buffers = NULL;
    
    return -ENOMEM;
}

/*
 * Clean up per-CPU shared buffers
 */
void cleanup_shared_buffer_array(HrperfSharedBufferArray *sbuf_array)
{
    int cpu;
    HrperfSharedBuffer *sbuf;
    
    if (!sbuf_array || !sbuf_array->buffers)
        return;
    
    for_each_online_cpu(cpu) {
        if (cpu >= sbuf_array->cpu_count)
            break;
            
        sbuf = &sbuf_array->buffers[cpu];
        
        if (sbuf->kernel_addr) {
            vfree(sbuf->kernel_addr);
            sbuf->kernel_addr = NULL;
        }
        
        if (sbuf->pages) {
            kfree(sbuf->pages);
            sbuf->pages = NULL;
        }
    }
    
    kfree(sbuf_array->buffers);
    sbuf_array->buffers = NULL;
}

/*
 * Write a log entry to a CPU's shared buffer
 */
int write_to_shared_buffer(HrperfSharedBuffer *sbuf, const HrperfLogEntry *entry)
{
    if (!sbuf || !sbuf->kernel_addr || !entry)
        return -EINVAL;
    
    // Get current metadata
    uint32_t *header = (uint32_t *)sbuf->kernel_addr;
    size_t entry_size = sizeof(HrperfLogEntry);
    size_t header_size = 16; // 16 bytes of header (4 uint32_t values)
    size_t data_area_size = sbuf->size - header_size;
    size_t max_entries = data_area_size / entry_size;
    
    // Get current write position
    uint32_t write_idx = atomic_read(&sbuf->write_idx);
    uint32_t slot = write_idx % max_entries;
    
    // Calculate address to write to
    char *write_addr = (char*)sbuf->kernel_addr + header_size + (slot * entry_size);
    
    // Copy the entry to the buffer
    memcpy(write_addr, entry, entry_size);
    
    // Update indices
    atomic_inc(&sbuf->write_idx);
    header[3] = atomic_read(&sbuf->write_idx); // Update write index in header
    
    // Memory barrier to ensure updates are visible
    smp_wmb();
    
    return 0;
}
