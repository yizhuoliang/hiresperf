#include <bpf/libbpf.h>
#include <bpf/bpf.h>
#include <stdio.h>
#include <unistd.h>

#include "hrp_bpf.h"

int hrp_bpf_event_callback(void *ctx, void *data, size_t size)
{
    struct hrp_bpf_event *e = (struct hrp_bpf_event *)data;
    printf("Timestamp: %llu, PID: %u, TID: %u, Type: %u, Size: %u\n",
           e->ts_ns, e->pid, e->tid, e->event_type, e->size_or_ret);
    return 0;
}

int main() {
    struct bpf_object *obj;
    struct bpf_program *prog_tcp_out_start, *prog_tcp_out_end;
    struct bpf_link *link_tcp_out_start, *link_tcp_out_end;
    struct bpf_map *map;
    struct ring_buffer *rb;
    int map_fd, ret;

    obj = bpf_object__open_file("hrp_bpf.o", NULL);
    bpf_object__load(obj);
    prog_tcp_out_start = bpf_object__find_program_by_name(obj, "kprobe_tcp_sendmsg");
    prog_tcp_out_end = bpf_object__find_program_by_name(obj, "kretprobe_tcp_sendmsg");
    link_tcp_out_start = bpf_program__attach(prog_tcp_out_start);
    link_tcp_out_start = bpf_program__attach(prog_tcp_out_end);
    map = bpf_object__find_map_by_name(obj, "hrp_bpf_rb_map");
    map_fd = bpf_map__fd(map);

    struct ring_buffer_opts opts = { .sz = sizeof(opts) };
    rb = ring_buffer__new(map_fd, hrp_bpf_event_callback, NULL, &opts);
    if (!rb) {
        fprintf(stderr, "Failed to create ring buffer\n");
        return 1;
    }

    printf("Hiresperf BPF program loaded and attached. Monitoring Net and Disk IO...\n");

    while (1) {
        // Poll ring buffer and consume data by the callback
        int handled = ring_buffer__poll(rb, 0); // 0 timeout so this doesn't block
        if (handled < 0) {
            fprintf(stderr, "Hiresperf BPF event callback errored! Stop polling!\n");
            break;
        }
        usleep(HRP_BPF_POLL_INTERVAL_US);
    }

    ring_buffer__free(rb);
    bpf_link__destroy(link_tcp_out_start);
    bpf_link__destroy(link_tcp_out_end);
    bpf_object__close(obj);

    return 0;
}