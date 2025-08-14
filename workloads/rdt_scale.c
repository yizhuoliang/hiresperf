#include "hrperf_api.h"

int main() {
  u32 scale_factor = hrperf_get_rdt_scale_factor();
  if (scale_factor == 0) {
    fprintf(stderr, "Failed to get RDT scaling factor. Is hrperf compiled RDT "
                    "enabled?\n");
    return -1;
  }
  printf("RDT Scaling Factor: %u\n", scale_factor);
  return 0;
}