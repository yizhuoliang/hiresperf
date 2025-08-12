#include "hrperf_api.h"

int main() {
  u32 max_rmid = hrperf_get_rdt_max_rmid();
  if (max_rmid == 0) {
    fprintf(
        stderr,
        "Failed to get RDT max RMID. Is hrperf compiled with RDT enabled?\n");
    return -1;
  }
  printf("RDT Max RMID: %u\n", max_rmid);
  return 0;
}