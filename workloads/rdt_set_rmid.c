#include "hrperf_api.h"
#include <getopt.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

void print_usage(const char *program_name) {
  printf("Usage: %s -r <rmid> -c <core_id>\n", program_name);
  printf("  -r <rmid>    : RMID\n");
  printf("  -c <core_id> : Core ID to set RMID\n");
  printf("  -h           : Show this help message\n");
}

int main(int argc, char *argv[]) {
  u32 rmid = 0;
  u32 core_id = 0;
  int rmid_set = 0;
  int core_id_set = 0;
  int opt;

  while ((opt = getopt(argc, argv, "r:c:h")) != -1) {
    switch (opt) {
    case 'r':
      rmid = (u32)strtoul(optarg, NULL, 10);
      rmid_set = 1;
      break;
    case 'c':
      core_id = (u32)strtoul(optarg, NULL, 10);
      core_id_set = 1;
      break;
    case 'h':
      print_usage(argv[0]);
      return 0;
    default:
      print_usage(argv[0]);
      return 1;
    }
  }

  if (!rmid_set || !core_id_set) {
    fprintf(stderr,
            "Error: Both -r (rmid) and -c (core_id) arguments are required.\n");
    print_usage(argv[0]);
    return 1;
  }

  printf("Setting RMID %u on core %u\n", rmid, core_id);

  int result = hrperf_set_rmid(core_id, rmid);
  if (result != 0) {
    fprintf(stderr,
            "Error: Failed to set RMID %u on core %u. Is hrperf compiled with "
            "RDT enabled?\n",
            rmid, core_id);
    return 1;
  }

  printf("Successfully set RMID %u on core %u\n", rmid, core_id);
  return 0;
}