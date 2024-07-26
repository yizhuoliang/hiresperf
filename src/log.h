#ifndef _HRP_LOG_H
#define _HRP_LOG_H

#include <linux/kernel.h>

#include "config.h"
#include "buffer.h"

// Structure for a binary log entry
typedef struct {
    int cpu_id;
    HrperfTick tick;
} HrperfLogEntry;

struct file* hrperf_init_log_file(void);
void log_and_clear(HrperfRingBuffer *rb, int cpu_id, struct file *file);
void hrperf_close_log_file(struct file *file);


#endif
