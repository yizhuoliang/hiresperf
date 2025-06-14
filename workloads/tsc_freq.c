#include "hrperf_api.h"

#include <stdint.h>
#include <inttypes.h>
#include <stdio.h>

int main() {
    uint64_t freq = hrperf_get_tsc_freq();
    printf("TSC Frequency (cycles/us): %" PRIu64 "\n", freq);
    return 0;
}
