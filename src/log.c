#include <linux/fs.h>
#include <linux/uaccess.h>
#include <asm/uaccess.h>

#include "log.h"
#include "config.h"

struct file* hrperf_init_log_file(void) {
    struct file *file;
    mm_segment_t old_fs;

    // open in append mode
    old_fs = get_fs();
    set_fs(KERNEL_DS);
    file = filp_open(HRP_LOG_PATH, O_WRONLY | O_CREAT | O_APPEND, 0666);
    set_fs(old_fs);

    if (IS_ERR(file)) {
        printk(KERN_ERR "Error opening the log file\n");
        return NULL;
    }

    return file;
}

void log_and_clear(HrperfRingBuffer *rb, int cpu_id, struct file *file) {
    mm_segment_t old_fs;
    HrperfLogEntry entry;

    if (file == NULL) {
        printk(KERN_ERR "Invalid file descriptor\n");
        return;
    }

    while (rb->head != rb->tail) {
        entry.cpu_id = cpu_id;
        entry.tick = rb->buffer[rb->head];

        old_fs = get_fs();
        set_fs(KERNEL_DS);
        vfs_write(file, (char *)&entry, sizeof(HrperfLogEntry), &file->f_pos);
        set_fs(old_fs);

        rb->head = (rb->head + 1) % HRP_BUFFER_SIZE;
    }

    printk(KERN_INFO "Buffer cleared\n");
}

void hrperf_close_log_file(struct file *file) {
    if (file != NULL) {
        filp_close(file, NULL);
    }
}
