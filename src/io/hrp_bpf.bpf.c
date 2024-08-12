#include <linux/bpf.h>
#include <bpf/bpf_helpers.h>
#include <linux/ptrace.h>
#include <linux/tcp.h>
// #include <linux/bio.h>
// #include <linux/bvec.h>
// #include <linux/blk_types.h>

#include "../config.h"
#include "hrp_bpf.h"

struct {
    __uint(type, BPF_MAP_TYPE_RINGBUF);
    __uint(max_entries, HRP_BPF_BUFFER_SIZE);
} hrp_bpf_rb_map SEC(".maps");

/*
    ---- TCP EVENTS ----
*/

SEC("kprobe/tcp_sendmsg")
int kprobe_tcp_sendmsg(struct pt_regs *ctx) {
    struct hrp_bpf_event *e;

    RB_RESERVE(e);

    // RDX for size, RBP for identification
    SET_EVENT_FIELDS(e, HRP_BPF_EVENT_TCP_OUT_START, ctx->rdx, ctx->rbp);

    RB_SUBMIT(e);
    return 0;
}

SEC("kretprobe/tcp_sendmsg")
int kretprobe_tcp_sendmsg(struct pt_regs *ctx) {
    struct hrp_bpf_event *e;

    RB_RESERVE(e);

    // RAX for return val, RBP for identification
    SET_EVENT_FIELDS(e, HRP_BPF_EVENT_TCP_OUT_END, ctx->rax, ctx->rbp);

    RB_SUBMIT(e);
    return 0;
}

SEC("kprobe/tcp_recvmsg")
int kprobe_tcp_recvmsg(struct pt_regs *ctx) {
    struct hrp_bpf_event *e;

    RB_RESERVE(e);

    // RDX for size, RBP for identification
    SET_EVENT_FIELDS(e, HRP_BPF_EVENT_TCP_IN_START, ctx->rdx, ctx->rbp);

    RB_SUBMIT(e);
    return 0;
}

SEC("kretprobe/tcp_recvmsg")
int kretprobe_tcp_recvmsg(struct pt_regs *ctx) {
    struct hrp_bpf_event *e;

    RB_RESERVE(e);

    // RAX for return val, RBP for identification
    SET_EVENT_FIELDS(e, HRP_BPF_EVENT_TCP_IN_END, ctx->rax, ctx->rbp);

    RB_SUBMIT(e);
    return 0;
}

/*
    ---- UDP EVENTS ----
*/

SEC("kprobe/udp_sendmsg")
int kprobe_udp_sendmsg(struct pt_regs *ctx) {
    struct hrp_bpf_event *e;

    RB_RESERVE(e);

    // RDX for size, RBP for identification
    SET_EVENT_FIELDS(e, HRP_BPF_EVENT_UDP_OUT_START, ctx->rdx, ctx->rbp);

    RB_SUBMIT(e);
    return 0;
}

SEC("kretprobe/udp_sendmsg")
int kretprobe_udp_sendmsg(struct pt_regs *ctx) {
    struct hrp_bpf_event *e;

    RB_RESERVE(e);

    // RAX for return val, RBP for identification
    SET_EVENT_FIELDS(e, HRP_BPF_EVENT_UDP_OUT_END, ctx->rax, ctx->rbp);

    RB_SUBMIT(e);
    return 0;
}

SEC("kprobe/udp_recvmsg")
int kprobe_udp_recvmsg(struct pt_regs *ctx) {
    struct hrp_bpf_event *e;

    RB_RESERVE(e);

    // RDX for size, RBP for identification
    SET_EVENT_FIELDS(e, HRP_BPF_EVENT_UDP_IN_START, ctx->rdx, ctx->rbp);

    RB_SUBMIT(e);
    return 0;
}

SEC("kretprobe/udp_recvmsg")
int kretprobe_udp_recvmsg(struct pt_regs *ctx) {
    struct hrp_bpf_event *e;

    RB_RESERVE(e);

    // RDX for size, RBP for identification
    SET_EVENT_FIELDS(e, HRP_BPF_EVENT_UDP_IN_END, ctx->rax, ctx->rbp);

    RB_SUBMIT(e);
    return 0;
}

// SEC("kprobe/submit_bio")
// int bpf_prog_submit_bio(struct pt_regs *ctx) {
//     struct bio *bio = GET_BIO(ctx, rsi);
    
//     struct hrp_bpf_event *e;

//     RB_RESERVE(e);

//     if (bio_op(bio) == REQ_OP_READ) {
//         // READ
//         SET_EVENT_FIELDS(e, HRP_BPF_EVENT_BLKIO_READ_START, (0UL | (bio->bi_iter.bi_size)), bio);
//     } else {
//         // WRITE
//         SET_EVENT_FIELDS(e, HRP_BPF_EVENT_BLKIO_WRITE_START, (0UL | (bio->bi_iter.bi_size)), bio);
//     }
    
//     return 0;
// }

// SEC("kprobe/bio_endio")
// int bpf_prog_bio_endio(struct pt_regs *ctx) {
//     struct bio *bio = (struct bio *)(ctx->rdi);
//     u64 pid_tgid = bpf_get_current_pid_tgid();
//     struct disk_io_data *data = start_times.lookup(&pid_tgid);

//     if (data) {
//         data->end_ns = bpf_ktime_get_ns();
//         // Now calculate the bandwidth
//         u64 duration_ns = data->end_ns - data->start_ns;
//         u64 bandwidth = data->size / (duration_ns / 1000000000); // size in bytes per second

//         // Possibly log this data to a BPF map or output it for user-space processing

//         start_times.delete(&pid_tgid);
//     }
//     return 0;
// }

char _license[] SEC("license") = "GPL";