#ifndef _HRP_BPF_LOG_H
#define _HRP_BPF_LOG_H

#include <stdatomic.h>

extern volatile atomic_size_t log_offset;

void* hrp_bpf_log_init();
void hrp_bpf_log_cleanup(void *log_base);
int hrp_bpf_event_callback(void *ctx, void *data, size_t size);

#endif // _HRP_BPF_LOG_H