#include <mbm/types.h>
#include <mbm/rmid.h>
#include "mbm/mbm.h"

struct mbm_manager g_mbm_mgr;

/*
 * Check if MBM is supported by reading CPUID following Intel's recommended
 * sequence (ref. 19.18.3):
 * 1. Execute CPUID with EAX=0 to discover cpuid_maxLeaf
 * 2. If cpuid_maxLeaf >= 7, execute CPUID with EAX=7, ECX=0 to verify PQM[bit
 * 12]
 * 3. If PQM=1, execute CPUID with EAX=0FH, ECX=0 to query available resource
 * types
 * 4. If L3[bit 1]=1, execute CPUID with EAX=0FH, ECX=1 for L3 CMT/MBM
 * capabilities
 * 5. Query additional resource types if reported in step 3
 */
static bool mbm_is_supported(void) {
  u32 eax, ebx, ecx, edx;
  u32 cpuid_maxleaf;

  cpuid(0x0, &eax, &ebx, &ecx, &edx);
  cpuid_maxleaf = eax;

  if (cpuid_maxleaf < 7)
    return false;

  cpuid_count(0x7, 0, &eax, &ebx, &ecx, &edx);
  if (!(ebx & (1 << 12))) /* PQM bit */
    return false;

  pr_info("hrperf: PQM supported, checking L3 CMT/MBM capabilities...\n");
  cpuid_count(0xF, 0, &eax, &ebx, &ecx, &edx);

  if (!(edx & (1 << 1))) /* L3 Cache Monitoring not supported */
    return false;

  return true;
}

void mbm_init_cap(void) {
  u32 eax, ebx, ecx, edx;
  memset(&g_mbm_mgr.cap, 0, sizeof(g_mbm_mgr.cap));
  struct mbm_cap *cap = &g_mbm_mgr.cap;

  cpuid_count(0xF, 1, &eax, &ebx, &ecx, &edx);
  if (!(edx & (1 << 0))) {
    pr_info("hrperf: L3 CMT not supported.\n");
  } else {
    cap->l3_cmt = true;
  }

  if (!(edx & (1 << 1))) {
    pr_info("hrperf: L3 Total BW not supported.\n");
  } else {
    cap->l3_mbm_total = true;
  }

  if (!(edx & (1 << 2))) {
    pr_info("hrperf: L3 Local BW not supported.\n");
  } else {
    cap->l3_mbm_local = true;
  }

  if (!(eax & (1 << 8))) {
    pr_info("hrperf: Overflow bit does not exist.\n");
  } else {
    cap->overflow_bit = true;
  }

  if (!(eax & (1 << 9))) {
    pr_info("hrperf: Non-CPU L3 CMT not supported.\n");
  } else {
    cap->non_cpu_l3_cmt = true;
  }

  if (!(eax & (1 << 10))) {
    pr_info("hrperf: Non-CPU L3 MBM not supported.\n");
  } else {
    cap->non_cpu_l3_mbm = true;
  }

  /* Extract counter width from bits 0-7 of EAX */
  u32 counter_width_offset = eax & 0xFF;
  cap->counter_width = 24 + counter_width_offset;

  g_mbm_mgr.scaling_factor = ebx;

  u32 max_rmid = ecx;
  if (max_rmid == 0 || max_rmid > MAX_RMID) {
    pr_err("hrperf: Invalid max RMID %u, using default %u\n", max_rmid,
           DEFAULT_MAX_RMID);
    g_mbm_mgr.max_rmid = DEFAULT_MAX_RMID;
  } else {
    g_mbm_mgr.max_rmid = max_rmid;
  }

  pr_info("hrperf: MBM capabilities: L3 CMT=%d, Total BW=%d, Local BW=%d, "
          "Overflow Bit=%d, Non-CPU CMT=%d, Non-CPU MBM=%d, Counter Width=%u, "
          "Scaling Factor=%u, Max RMID=%u\n",
          cap->l3_cmt, cap->l3_mbm_total, cap->l3_mbm_local, cap->overflow_bit,
          cap->non_cpu_l3_cmt, cap->non_cpu_l3_mbm, cap->counter_width,
          g_mbm_mgr.scaling_factor, g_mbm_mgr.max_rmid);
}

int mbm_init(void) {
  if (!mbm_is_supported()) {
    return -ENODEV;
  }

  mbm_init_cap();
  mbm_rmid_init();
  
  return 0;
}

int mbm_deinit(void) {
  mbm_rmid_deinit();
  return 0;
}