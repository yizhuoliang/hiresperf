#include <linux/bpf.h>
#include <bpf/bpf_helpers.h>
#include <linux/ptrace.h>
#include <linux/tcp.h>   // For TCP socket operations

struct pid_tid {
    __u32 pid;
    __u32 tid;
};

struct bpf_map_def SEC("maps") traffic_count = {
    .type = BPF_MAP_TYPE_HASH,
    .key_size = sizeof(struct pid_tid),
    .value_size = sizeof(__u64),
    .max_entries = 1024,
};

SEC("kprobe/tcp_sendmsg")
int kprobe_tcp_sendmsg(struct pt_regs *ctx) {
    struct pid_tid key = {};
    __u64 zero = 0;
    __u64 *val;

    __u64 pidtgid = bpf_get_current_pid_tgid();
    key.pid = pidtgid >> 32;  // Extract PID from the upper 32 bits
    key.tid = pidtgid;  // Extract TID from the lower 32 bits

    // Look up the current value in the map
    val = bpf_map_lookup_elem(&traffic_count, &key);
    if (!val) {
        // Key not found, initialize it in the map
        bpf_map_update_elem(&traffic_count, &key, &zero, BPF_NOEXIST);
        val = bpf_map_lookup_elem(&traffic_count, &key);
    }
    if (val) {
        // Increment the stored value
        (*val)++;
    }

    return 0;
}

char _license[] SEC("license") = "GPL";