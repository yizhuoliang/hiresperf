#include <stdio.h>
#include "hrp_bpf_api.h"

int main() {
    hrp_bpf_start();
    printf("Monitoring started. Press enter to stop...\n");
    getchar();
    hrp_bpf_stop();
    printf("Monitoring stopped.\n");
    return 0;
}