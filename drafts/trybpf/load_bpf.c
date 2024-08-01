#include <bpf/libbpf.h>
#include <stdio.h>

int main(int argc, char **argv) {
    struct bpf_object *obj;
    struct bpf_program *prog;
    struct bpf_link *link;
    int ret;

    // Load the BPF program from file
    obj = bpf_object__open_file("net_traffic_monitor.o", NULL);
    if (libbpf_get_error(obj)) {
        fprintf(stderr, "Error opening BPF object file\n");
        return 1;
    }

    // Load BPF program into the kernel
    ret = bpf_object__load(obj);
    if (ret) {
        fprintf(stderr, "Error loading BPF object\n");
        return 1;
    }

    // Attach BPF program to a hook (e.g., kprobe)
    prog = bpf_object__find_program_by_name(obj, "kprobe_tcp_sendmsg");
    if (!prog) {
        fprintf(stderr, "Error finding BPF program in object\n");
        return 1;
    }

    link = bpf_program__attach(prog);
    if (libbpf_get_error(link)) {
        fprintf(stderr, "Error attaching BPF program\n");
        return 1;
    }

    printf("BPF program loaded and attached successfully\n");

    // Normally you would do more work here, or wait for events
    // For this example, we'll just sleep for a bit
    sleep(30);

    // Cleanup
    bpf_link__destroy(link);
    bpf_object__close(obj);

    return 0;
}