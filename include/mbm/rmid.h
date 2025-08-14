/*
 * This file defines RMID management structures and functions
 * for per-core memory bandwidth monitoring using Intel MBM.
 */

#ifndef _MBM_RMID_H_
#define _MBM_RMID_H_

#include "mbm/counter.h"
#include "mbm/types.h"

#define RMID0 0

extern struct mbm_manager g_mbm_mgr;

int mbm_rmid_init(void);
void mbm_rmid_deinit(void);

int mbm_rmid_clear_assoc(void);
int mbm_rmid_allocate_all_cores(void);
void mbm_rmid_deallocate_all(void);
u32 mbm_rmid_get_for_core(u32 core_id);

static __always_inline void mbm_set_rmid(u32 rmid) {
  u64 val;

  rdmsrl(MSR_IA32_PQR_ASSOC, val);

  val &= ~PQR_ASSOC_RMID_MASK;
  val |= (rmid & PQR_ASSOC_RMID_MASK);

  wrmsrl(MSR_IA32_PQR_ASSOC, val);
}

static __always_inline void mbm_set_rmid_smp(void *info) {
  u32 rmid = *(u32 *)info;
  mbm_set_rmid(rmid);
}

/*
 * mbm_set_rmid_for_core - Set RMID for a specific core.
 * @core_id: The ID of the core to set the RMID for.
 * @rmid: The RMID to set.
 *
 * This function sets the RMID for the specified core.
 *
 * WARNING: This function is NOT thread-safe and is intended for testing
 * and initialization purposes only. It should not be called concurrently
 * from multiple threads or in a production environment where race conditions
 * could lead to data corruption.
 *
 * If mbm_get_rmid_info_for_core returns NULL, it indicates a critical
 * setup error (e.g., invalid core_id or RMID table corruption), and the
 * system is expected to panic upon dereferencing the NULL pointer.
 */
static __always_inline void mbm_set_rmid_for_core(u32 core_id, u32 rmid) {
  smp_call_function_single(core_id, mbm_set_rmid_smp, &rmid, 1);
  struct rmid_info *info = mbm_get_rmid_info_for_core(core_id);
  info->rmid = rmid;
  info->in_use = true;
}

static __always_inline void mbm_reset_rmid(void *info) { mbm_set_rmid(RMID0); }

#endif /* _MBM_RMID_H_ */
