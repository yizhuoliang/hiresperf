/*
 * This file defines RMID management structures and functions
 * for per-core memory bandwidth monitoring using Intel MBM.
 */

#ifndef _MBM_RMID_H_
#define _MBM_RMID_H_

#include "mbm/types.h"


#define RMID0 0

extern struct mbm_manager g_mbm_mgr;

int mbm_rmid_init(void);
void mbm_rmid_deinit(void);

int mbm_rmid_clear_assoc(void);
int mbm_rmid_allocate_per_core(void);
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

static __always_inline void mbm_reset_rmid(void *info) { mbm_set_rmid(RMID0); }

#endif /* _MBM_RMID_H_ */