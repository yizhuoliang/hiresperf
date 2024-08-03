#ifndef _HRP_BPF_H
#define _HRP_BPF_H

#define HRP_BPF_EVENT_TCP_IN_START 1
#define HRP_BPF_EVENT_TCP_OUT_START 2
#define HRP_BPF_EVENT_UDP_IN_START 3
#define HRP_BPF_EVENT_UDP_OUT_START 4
#define HRP_BPF_EVENT_BLKIO_READ_START 5
#define HRP_BPF_EVENT_BLKIO_READ_START 6

#define HRP_BPF_EVENT_TCP_IN_END 7
#define HRP_BPF_EVENT_TCP_OUT_END 8
#define HRP_BPF_EVENT_UDP_IN_END 9
#define HRP_BPF_EVENT_UDP_OUT_END 10
#define HRP_BPF_EVENT_BLKIO_READ_END 11
#define HRP_BPF_EVENT_BLKIO_READ_END 12

struct hrp_bpf_event {
    unsigned long long ts_ns;
    unsigned int pid;
    unsigned int tid;
    unsigned int event_type;
    unsigned int size_or_ret;
};

// This polling interval doesn't affect the timeline accuracy,
// since each event has timestamps of start and return times
#define HRP_BPF_BUFFER_SIZE 10240
#define HRP_BPF_POLL_INTERVAL_US 1000

// the ssh process by default send tcp every 100us, pollutes the log
// so a non-zero value here will exclude the process from being logged
#define HPR_BPF_SSH_PID 0

#define HRP_BPF_LOG_PATH "/hrperf_bpf_log.bin"

#endif