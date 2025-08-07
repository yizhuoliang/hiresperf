/*
 * This file implements RMID allocation, deallocation, and management
 * for per-core memory bandwidth monitoring.
 */

#include <asm/msr.h>
#include <linux/cpumask.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/smp.h>

#include "mbm/rmid.h"

static struct mbm_manager g_mbm_mgr;

/*
 * Clear all RMID associations by setting RMID to 0 on all online cores
 */
int mbm_rmid_clear_assoc(void) {
  u32 rmid;
  int cpu;
  struct rmid_manager *g_rmid_mgr = &g_mbm_mgr.rmid_mgr;

  spin_lock(&g_rmid_mgr->lock);

  for (rmid = 0; rmid <= g_mbm_mgr.max_rmid; rmid++) {
    for_each_online_cpu(cpu) {
      smp_call_function_single(cpu, mbm_reset_rmid, NULL, 1);
    }
  }

  spin_unlock(&g_rmid_mgr->lock);
  return 0;
}

/*
 * Allocate RMID for each core
 */
int mbm_rmid_allocate_per_core(void) {
  u32 cpu, core_id = 0;
  struct rmid_info *rmid_info;
  struct rmid_manager *g_rmid_mgr = &g_mbm_mgr.rmid_mgr;

  spin_lock(&g_rmid_mgr->lock);

  /* Clear all existing assignments first */
  for (cpu = 0; cpu < g_rmid_mgr->num_cores; cpu++) {
    g_rmid_mgr->rmid_table[cpu].in_use = false;
    g_rmid_mgr->rmid_table[cpu].rmid = 0;
  }

  /* Assign unique RMID to each online core */
  for_each_online_cpu(cpu) {
    if (core_id > g_mbm_mgr.max_rmid) {
      pr_warn("Not enough RMIDs for all cores, stopping at core %u\n", core_id);
      break;
    }

    rmid_info = &g_rmid_mgr->rmid_table[core_id];
    rmid_info->rmid = core_id + 1; /* RMID 0 is reserved */
    rmid_info->core_id = cpu;
    rmid_info->in_use = true;
    rmid_info->last_total_bytes = 0;
    rmid_info->last_local_bytes = 0;

    smp_call_function_single(cpu, mbm_set_rmid_smp, &rmid_info->rmid, 1);

    core_id++;
  }

  g_rmid_mgr->num_cores = core_id;
  spin_unlock(&g_rmid_mgr->lock);

  pr_info("Allocated RMIDs for %u cores\n", core_id);
  return 0;
}

/*
 * Deallocate all RMIDs
 */
void mbm_rmid_deallocate_all(void) {
  u32 i;
  struct rmid_manager *g_rmid_mgr = &g_mbm_mgr.rmid_mgr;

  spin_lock(&g_rmid_mgr->lock);

  /* Reset RMID association on all cores */
  for (i = 0; i < g_rmid_mgr->num_cores; i++) {
    if (g_rmid_mgr->rmid_table[i].in_use) {
      smp_call_function_single(g_rmid_mgr->rmid_table[i].core_id,
                               mbm_reset_rmid, NULL, 1);
      g_rmid_mgr->rmid_table[i].in_use = false;
    }
  }

  g_rmid_mgr->num_cores = 0;
  spin_unlock(&g_rmid_mgr->lock);

  pr_info("Deallocated all RMIDs\n");
}

/*
 * Get RMID for a specific core
 */
u32 mbm_rmid_get_for_core(u32 core_id) {
  u32 i;
  u32 rmid = 0;

  struct rmid_manager *g_rmid_mgr = &g_mbm_mgr.rmid_mgr;

  spin_lock(&g_rmid_mgr->lock);

  for (i = 0; i < g_rmid_mgr->num_cores; i++) {
    if (g_rmid_mgr->rmid_table[i].in_use &&
        g_rmid_mgr->rmid_table[i].core_id == core_id) {
      rmid = g_rmid_mgr->rmid_table[i].rmid;
      break;
    }
  }

  spin_unlock(&g_rmid_mgr->lock);
  return rmid;
}

/*
 * Initialize RMID management
 */
int mbm_rmid_init(void) {
  int ret;
  struct rmid_manager *g_rmid_mgr = &g_mbm_mgr.rmid_mgr;

  memset(g_rmid_mgr, 0, sizeof(*g_rmid_mgr));
  spin_lock_init(&g_rmid_mgr->lock);

  g_rmid_mgr->rmid_table =
      kcalloc(MAX_CORES, sizeof(struct rmid_info), GFP_KERNEL);
  if (!g_rmid_mgr->rmid_table) {
    pr_err("Failed to allocate RMID table\n");
    return -ENOMEM;
  }

  ret = mbm_rmid_clear_assoc();
  if (ret) {
    pr_warn("Failed to clear RMID counters: %d\n", ret);
  }

  pr_info("RMID manager initialized, max RMID: %u\n", g_mbm_mgr.max_rmid);
  return 0;
}

/*
 * Deinit RMID management
 */
void mbm_rmid_deinit(void) {
  mbm_rmid_deallocate_all();
  struct rmid_manager *g_rmid_mgr = &g_mbm_mgr.rmid_mgr;

  if (g_rmid_mgr->rmid_table) {
    kfree(g_rmid_mgr->rmid_table);
    g_rmid_mgr->rmid_table = NULL;
  }

  pr_info("RMID manager cleaned up\n");
}

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
  pr_info("CPUID 0x7: EAX=%08x EBX=%08x ECX=%08x EDX=%08x\n", eax, ebx, ecx,
          edx);
  if (!(ebx & (1 << 12))) /* PQM bit */
    return false;

  pr_info("PQM supported, checking L3 CMT/MBM capabilities...\n");
  cpuid_count(0xF, 0, &eax, &ebx, &ecx, &edx);

  pr_info("CPUID 0xF: EAX=%08x EBX=%08x ECX=%08x EDX=%08x\n", eax, ebx, ecx,
          edx);
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
    pr_info("L3 CMT not supported.\n");
  } else {
    cap->l3_cmt = true;
  }

  if (!(edx & (1 << 1))) {
    pr_info("L3 Total BW not supported.\n");
  } else {
    cap->l3_mbm_total = true;
  }

  if (!(edx & (1 << 2))) {
    pr_info("L3 Local BW not supported.\n");
  } else {
    cap->l3_mbm_local = true;
  }

  if (!(eax & (1 << 8))) {
    pr_info("Overflow bit does not exist.\n");
  } else {
    cap->overflow_bit = true;
  }

  if (!(eax & (1 << 9))) {
    pr_info("Non-CPU L3 CMT not supported.\n");
  } else {
    cap->non_cpu_l3_cmt = true;
  }

  if (!(eax & (1 << 10))) {
    pr_info("Non-CPU L3 MBM not supported.\n");
  } else {
    cap->non_cpu_l3_mbm = true;
  }

  /* Extract counter width from bits 0-7 of EAX */
  u32 counter_width_offset = eax & 0xFF;
  cap->counter_width = 24 + counter_width_offset;
  pr_info("Counter width: %u bits\n", cap->counter_width);

  g_mbm_mgr.scaling_factor = ebx;

  u32 max_rmid = ecx;
  if (max_rmid == 0 || max_rmid > MAX_RMID) {
    pr_err("Invalid max RMID %u, using default %u\n", max_rmid,
           DEFAULT_MAX_RMID);
    g_mbm_mgr.max_rmid = DEFAULT_MAX_RMID;
  } else {
    g_mbm_mgr.max_rmid = max_rmid;
  }

  pr_info("MBM capabilities initialized: L3 CMT=%d, Total BW=%d, Local BW=%d, "
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
