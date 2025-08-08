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
  u32 cpu, num_cores = 0;
  struct rmid_info *rmid_info;
  struct rmid_manager *g_rmid_mgr = &g_mbm_mgr.rmid_mgr;

  spin_lock(&g_rmid_mgr->lock);

  /* Clear all existing assignments first */
  for (cpu = 0; cpu < MAX_CORES; cpu++) {
    g_rmid_mgr->rmid_table[cpu].in_use = false;
    g_rmid_mgr->rmid_table[cpu].rmid = 0;
  }

  /* Assign unique RMID to each online core */
  for_each_online_cpu(cpu) {
    if (num_cores > g_mbm_mgr.max_rmid) {
      pr_warn("Not enough RMIDs for all cores, stopping at core id %u\n", cpu);
      break;
    }

    rmid_info = &g_rmid_mgr->rmid_table[num_cores];
    rmid_info->rmid = cpu + 1; /* RMID 0 is reserved */
    rmid_info->core_id = cpu;
    rmid_info->in_use = true;
    rmid_info->last_total_bytes = 0;
    rmid_info->last_local_bytes = 0;

    smp_call_function_single(cpu, mbm_set_rmid_smp, &rmid_info->rmid, 1);

    num_cores++;
  }

  g_rmid_mgr->num_cores = num_cores;
  spin_unlock(&g_rmid_mgr->lock);

  pr_info("Allocated RMIDs for %u cores\n", num_cores);
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
  u32 rmid = RMID0;

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
