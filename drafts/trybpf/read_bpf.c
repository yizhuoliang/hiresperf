#include <bpf/libbpf.h>
#include <bpf/bpf.h>
#include <stdio.h>
#include <unistd.h>

struct pid_tid {
    __u32 pid;
    __u32 tid;
};

int main() {
    struct bpf_object *obj;
    struct bpf_program *prog;
    struct bpf_link *link;
    struct bpf_map *map;
    int map_fd, ret;
    struct pid_tid key, next_key;
    __u64 value;

    obj = bpf_object__open_file("net_traffic_monitor.o", NULL);
    ret = bpf_object__load(obj);
    prog = bpf_object__find_program_by_name(obj, "kprobe_tcp_sendmsg");
    link = bpf_program__attach(prog);

    // Find the map by name
    map = bpf_object__find_map_by_name(obj, "traffic_count");
    map_fd = bpf_map__fd(map);

    printf("BPF program loaded and attached. Monitoring traffic...\n");

    while (1) {
        // Read map entries
        while (bpf_map_get_next_key(map_fd, &key, &next_key) == 0) {
            bpf_map_lookup_elem(map_fd, &next_key, &value);
            printf("PID: %u, TID: %u, Traffic Count: %llu\n", next_key.pid, next_key.tid, value);

            // Set the current key to next key for the subsequent read
            key = next_key;
        }
        // Reset the map for next interval
        bpf_map_delete_elem(map_fd, &next_key);
        sleep(10); // Wait for 10 seconds before reading again
    }

    bpf_link__destroy(link);
    bpf_object__close(obj);

    return 0;
}