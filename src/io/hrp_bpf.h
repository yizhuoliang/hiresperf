#ifndef _HRP_BPF_H
#define _HRP_BPF_H

#define HRP_BPF_EVENT_TCP_IN_START 1
#define HRP_BPF_EVENT_TCP_OUT_START 2
#define HRP_BPF_EVENT_UDP_IN_START 3
#define HRP_BPF_EVENT_UDP_OUT_START 4
#define HRP_BPF_EVENT_BLKIO_READ_START 5
#define HRP_BPF_EVENT_BLKIO_WRITE_START 6

#define HRP_BPF_EVENT_TCP_IN_END 7
#define HRP_BPF_EVENT_TCP_OUT_END 8
#define HRP_BPF_EVENT_UDP_IN_END 9
#define HRP_BPF_EVENT_UDP_OUT_END 10
#define HRP_BPF_EVENT_BLKIO_READ_END 11
#define HRP_BPF_EVENT_BLKIO_WRITE_END 12

struct hrp_bpf_event {
    unsigned long long ts_ns;
    unsigned int pid;
    unsigned int tid;
    unsigned int event_type;
    // probably we should only use the ret when parsing
    unsigned long long size_or_ret;
    // If the calls are executed sequntially on each thread, then this is not needed,
    // just put this here for now. RBP for net calls, bio address for block IO.
    // Also, we don't need the generation number because there's no sampling.
    unsigned long long rbp_or_req_addr;
};

#define RB_RESERVE(E) \
    E = bpf_ringbuf_reserve(&hrp_bpf_rb_map, sizeof(*E), 0); \
    if (!E) return 0

#define RB_SUBMIT(E) \
    bpf_ringbuf_submit(E, 0)

#define SET_EVENT_FIELDS(E, TYPE, SIZE_OR_RET, RBP_OR_REQ_ADDR) \
    E->ts_ns = bpf_ktime_get_ns(); \
    unsigned long long pid_tgid = bpf_get_current_pid_tgid(); \
    E->pid = pid_tgid >> 32; \
    E->tid = pid_tgid & 0xFFFFFFFF; \
    E->event_type = TYPE; \
    E->size_or_ret = (unsigned int)SIZE_OR_RET; \
    E->rbp_or_req_addr = (unsigned long long)RBP_OR_REQ_ADDR

#define GET_BIO(CTX, BIO_REGISTER) \
    (struct bio *)(ctx->rsi); \
    if (!bio) return 0

// Warning: this is not safe in the long term, should still use linux headers
// temperorily just use place holders for values before the length
struct request {
    // place holders, never access!
	void *q;
	void *mq_ctx;
	void *mq_hctx;
	unsigned int cmd_flags;	
	unsigned int rq_flags;
	int tag;
	int internal_tag;

    // this is what we want
    unsigned int __data_len;

    // other fileds are not needed
};

#endif