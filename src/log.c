#include <linux/fs.h>
#include <linux/uaccess.h>
#include <linux/kernel.h>
#include <linux/printk.h>
#include <linux/smp.h>

#include "log.h"
#include "config.h"

struct file* hrperf_init_log_file(void) {
    struct file *file;

    file = filp_open(HRP_PMC_LOG_PATH, O_WRONLY | O_CREAT | O_APPEND | O_LARGEFILE, 0666);
    if (IS_ERR(file)) {
        printk(KERN_ERR "Error opening the log file\n");
        return NULL;
    }

    return file;
}

inline __attribute__((always_inline)) void log_and_clear(HrperfRingBuffer *rb, struct file *file) {
    unsigned int head, tail;
    ssize_t write_ret;

    head = rb->head;
    tail = smp_load_acquire(&rb->tail);

    if (head == tail) {
        // Buffer is empty
        return;
    }

    if (head < tail) {
        // Data is contiguous
        size_t size = (tail - head) * sizeof(HrperfLogEntry);
        write_ret = kernel_write(file, &rb->buffer[head], size, &file->f_pos);
        if (write_ret < 0) {
            printk(KERN_ERR "hrperf: kernel_write error: %zd\n", write_ret);
        }
    } else {
        // Data wraps around
        size_t size1 = (HRP_PMC_BUFFER_SIZE - head) * sizeof(HrperfLogEntry);
        write_ret = kernel_write(file, &rb->buffer[head], size1, &file->f_pos);
        if (write_ret < 0) {
            printk(KERN_ERR "hrperf: kernel_write error: %zd\n", write_ret);
        }

        if (tail > 0) {
            size_t size2 = tail * sizeof(HrperfLogEntry);
            write_ret = kernel_write(file, &rb->buffer[0], size2, &file->f_pos);
            if (write_ret < 0) {
                printk(KERN_ERR "hrperf: kernel_write error: %zd\n", write_ret);
            }
        }
    }

    // Update head
    smp_store_release(&rb->head, tail);
}

void hrperf_close_log_file(struct file *file) {
    if (file != NULL) {
        filp_close(file, NULL);
    }
}
