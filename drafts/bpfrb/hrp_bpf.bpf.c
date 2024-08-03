#include <linux/bpf.h>
#include <bpf/bpf_helpers.h>
#include <linux/ptrace.h>
#include <linux/tcp.h>

#include "hrp_bpf.h"

struct {
    __uint(type, BPF_MAP_TYPE_RINGBUF);
    __uint(max_entries, HRP_BPF_BUFFER_SIZE);
} hrp_bpf_rb_map SEC(".maps");

SEC("kprobe/tcp_sendmsg")
int kprobe_tcp_sendmsg(struct pt_regs *ctx) {
    struct hrp_bpf_event *e;

    e = bpf_ringbuf_reserve(&hrp_bpf_rb_map, sizeof(*e), 0);
    if (!e)
        return 0;

    unsigned long long pid_tgid = bpf_get_current_pid_tgid();
    e->ts_ns = bpf_ktime_get_ns();
    e->pid = pid_tgid >> 32;
    e->tid = pid_tgid & 0xFFFFFFFF;
    e->event_type = HRP_BPF_EVENT_TCP_OUT_START;
    e->size_or_ret = (unsigned int)ctx->rdx; // Get the size from RDX

    bpf_ringbuf_submit(e, 0);
    return 0;
}

SEC("kretprobe/tcp_sendmsg")
int kretprobe_tcp_sendmsg(struct pt_regs *ctx) {
    struct hrp_bpf_event *e;
    e = bpf_ringbuf_reserve(&hrp_bpf_rb_map, sizeof(*e), 0);
    if (!e)
        return 0;

    e->ts_ns = bpf_ktime_get_ns();
    e->pid = bpf_get_current_pid_tgid() >> 32;
    e->tid = bpf_get_current_pid_tgid() & 0xFFFFFFFF;
    e->event_type = HRP_BPF_EVENT_TCP_OUT_END;
    e->size_or_ret = (unsigned int)ctx->rax;  // Get the return value from RAX

    bpf_ringbuf_submit(e, 0);
    return 0;
}

char _license[] SEC("license") = "GPL";