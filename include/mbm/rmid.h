/*
 * This file defines RMID management structures and functions
 * for per-core memory bandwidth monitoring using Intel MBM.
 */

#ifndef _MBM_RMID_H_
#define _MBM_RMID_H_

#include "mbm/types.h"

/* MSR field definitions */
#define PQR_ASSOC_RMID_MASK 0x3FF
#define QM_EVTSEL_RMID_SHIFT 32
#define QM_EVTSEL_RMID_MASK 0x3FF
#define QM_EVTSEL_EVENT_MASK 0xFF
#define QM_CTR_DATA_MASK 0x3FFFFFFFFFFFFFFFULL
#define QM_CTR_ERROR_MASK (1ULL << 63)
#define QM_CTR_UNAVAIL_MASK (1ULL << 62)

/* MBM event types */
#define MBM_EVENT_L3_OCCUP 0x01
#define MBM_EVENT_L3_TOTAL_BW 0x02
#define MBM_EVENT_L3_LOCAL_BW 0x03

/* Maximum supported values */
#define MAX_RMID 1023
#define MAX_CORES 256
#define DEFAULT_MAX_RMID 255

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