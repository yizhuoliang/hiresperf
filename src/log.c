#include <linux/fs.h>
#include <linux/uaccess.h>

#include "log.h"
#include "config.h"

struct file* hrperf_init_log_file(void) {
    struct file *file;

    file = filp_open(HRP_LOG_PATH, O_WRONLY | O_CREAT | O_APPEND, 0666);
    if (IS_ERR(file)) {
        printk(KERN_ERR "Error opening the log file\n");
        return NULL;
    }

    return file;
}

void log_and_clear(HrperfRingBuffer *rb, int cpu_id, struct file *file) {
    HrperfLogEntry entry;

    if (file == NULL) {
        printk(KERN_ERR "Invalid file descriptor\n");
        return;
    }

    while (rb->head != rb->tail) {
        entry.cpu_id = cpu_id;
        entry.tick = rb->buffer[rb->head];

        kernel_write(file, &entry, sizeof(HrperfLogEntry), &file->f_pos);

        rb->head = (rb->head + 1) % HRP_BUFFER_SIZE;
    }
}

void hrperf_close_log_file(struct file *file) {
    if (file != NULL) {
        filp_close(file, NULL);
    }
}
