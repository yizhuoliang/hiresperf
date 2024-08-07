#include <bpf/libbpf.h>
#include <bpf/bpf.h>
#include <stdio.h>
#include <unistd.h>
#include <sys/mman.h>
#include <stdbool.h>
#include <pthread.h>

#include "hrp_bpf.h"
#include "log.h"
#include "hrp_bpf_api.h"

// Global variables for BPF components
static struct bpf_object *obj = NULL;
static struct bpf_program *prog_tcp_out_start = NULL, *prog_tcp_out_end = NULL,
                         *prog_tcp_in_start = NULL, *prog_tcp_in_end = NULL,
                         *prog_udp_out_start = NULL, *prog_udp_out_end = NULL,
                         *prog_udp_in_start = NULL, *prog_udp_in_end = NULL;
static struct bpf_link *link_tcp_out_start = NULL, *link_tcp_out_end = NULL,
                       *link_tcp_in_start = NULL, *link_tcp_in_end = NULL,
                       *link_udp_out_start = NULL, *link_udp_out_end = NULL,
                       *link_udp_in_start = NULL, *link_udp_in_end = NULL;
static struct ring_buffer *rb = NULL;
static void *log_base = NULL;
static volatile bool running = true;
static pthread_t hrp_bpf_polling_thread;

int hrp_bpf_init_log_and_programs() {
    // Step 0: init logging
    log_base = hrp_bpf_log_init();
    if (!log_base) {
        return 1;
    }
    atomic_init(&log_offset, 0);

    // Step 1: load BPF object
    obj = bpf_object__open_file("hrp_bpf.o", NULL);
    if (obj == NULL) {
        hrp_bpf_log_cleanup(log_base);
        fprintf(stderr, "Failed to create load the bpf object file\n");
        return 1;
    }
    bpf_object__load(obj);

    // Step 2: extract BPF programs
    if (HRP_BPF_ENABLE_TCP) {
        prog_tcp_out_start = bpf_object__find_program_by_name(obj, "kprobe_tcp_sendmsg");
        prog_tcp_out_end = bpf_object__find_program_by_name(obj, "kretprobe_tcp_sendmsg");
        prog_tcp_in_start = bpf_object__find_program_by_name(obj, "kprobe_tcp_recvmsg");
        prog_tcp_in_end = bpf_object__find_program_by_name(obj, "kretprobe_tcp_recvmsg");
    }

    if (HRP_BPF_ENABLE_UDP) {
        prog_udp_out_start = bpf_object__find_program_by_name(obj, "kprobe_udp_sendmsg");
        prog_udp_out_end = bpf_object__find_program_by_name(obj, "kretprobe_udp_sendmsg");
        prog_udp_in_start = bpf_object__find_program_by_name(obj, "kprobe_udp_recvmsg");
        prog_udp_in_end = bpf_object__find_program_by_name(obj, "kretprobe_udp_recvmsg");
    }
    return 0;
}

void *hrp_bpf_attach_and_poll(void *arg) {
    // finally attach BPF programs
    if (prog_tcp_out_start) link_tcp_out_start = bpf_program__attach(prog_tcp_out_start);
    if (prog_tcp_out_end) link_tcp_out_end = bpf_program__attach(prog_tcp_out_end);
    if (prog_tcp_in_start) link_tcp_in_start = bpf_program__attach(prog_tcp_in_start);
    if (prog_tcp_in_end) link_tcp_in_end = bpf_program__attach(prog_tcp_in_end);

    if (prog_udp_out_start) link_udp_out_start = bpf_program__attach(prog_udp_out_start);
    if (prog_udp_out_end) link_udp_out_end = bpf_program__attach(prog_udp_out_end);
    if (prog_udp_in_start) link_udp_in_start = bpf_program__attach(prog_udp_in_start);
    if (prog_udp_in_end) link_udp_in_end = bpf_program__attach(prog_udp_in_end);

    // initialize ring buffer
    struct bpf_map *map = bpf_object__find_map_by_name(obj, "hrp_bpf_rb_map");
    int map_fd = bpf_map__fd(map);
    struct ring_buffer_opts opts = {.sz = sizeof(opts)};
    rb = ring_buffer__new(map_fd, hrp_bpf_event_callback, log_base, &opts);
    if (!rb) {
        fprintf(stderr, "Failed to create ring buffer\n");
        return (void *)1;
    }

    printf("Hiresperf BPF programs loaded and attached. Monitoring Net and Disk IO...\n");

    while (running) {
        // poll ring buffer will consume data by the callback
        int handled = ring_buffer__poll(rb, 0); // 0 timeout so this doesn't block
        if (handled < 0) {
            fprintf(stderr, "Hiresperf BPF event callback errored! Stop polling!\n");
            break;
        }
        usleep(HRP_BPF_POLL_INTERVAL_US);
    }

    return 0;
}

int hrp_bpf_start() {
    int ret = hrp_bpf_init_log_and_programs();
    if (ret != 0) {
        fprintf(stderr, "Hiresperf BPF programs failed to init.\n");
        return 1;
    }

    // launch the polling thread
    if (pthread_create(&hrp_bpf_polling_thread, NULL, hrp_bpf_attach_and_poll, NULL) != 0) {
        fprintf(stderr, "Hiresperf BPF failed to start polling thread.\n");
        return 1;
    }
}

void hrp_bpf_stop() {
    running = false;
    pthread_join(hrp_bpf_polling_thread, NULL);

    if (rb) ring_buffer__free(rb);
    if (link_tcp_out_start) bpf_link__destroy(link_tcp_out_start);
    if (link_tcp_out_end) bpf_link__destroy(link_tcp_out_end);
    if (link_tcp_in_start) bpf_link__destroy(link_tcp_in_start);
    if (link_tcp_in_end) bpf_link__destroy(link_tcp_in_end);
    if (link_udp_out_start) bpf_link__destroy(link_udp_out_start);
    if (link_udp_out_end) bpf_link__destroy(link_udp_out_end);
    if (link_udp_in_start) bpf_link__destroy(link_udp_in_start);
    if (link_udp_in_end) bpf_link__destroy(link_udp_in_end);
    if (obj) bpf_object__close(obj);
    if (log_base) hrp_bpf_log_cleanup(log_base);
}

int main() {
    hrp_bpf_start();
    printf("Monitoring started. Press enter to stop...\n");
    getchar();
    hrp_bpf_stop();
    printf("Monitoring stopped.\n");
    return 0;
}
