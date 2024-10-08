#ifndef LOG_H
#define LOG_H

#include <linux/fs.h>
#include "buffer.h"

struct file* hrperf_init_log_file(void);
void log_and_clear(HrperfRingBuffer *rb, struct file *file);
void hrperf_close_log_file(struct file *file);

#endif // LOG_H
