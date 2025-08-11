#ifndef _MBM_TYPES_H_
#define _MBM_TYPES_H_

#include <asm/msr-index.h>
#include <linux/spinlock.h>
#include <linux/types.h>

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

#define LOWER_32(val) ((u32)((val) & 0xFFFFFFFFULL))
#define UPPER_32(val) ((u32)(((val) >> 32) & 0xFFFFFFFFULL))

/* RMID allocation structure */
struct rmid_info {
  u32 rmid;
  u32 core_id;
  bool in_use;
  u64 last_total_bytes;
  u64 last_local_bytes;
};

/* Global RMID management structure */
struct rmid_manager {
  u32 num_cores;
  struct rmid_info *rmid_table;
  spinlock_t lock;
};

struct mbm_manager {
  struct rmid_manager rmid_mgr;
  struct mbm_cap {
    bool l3_cmt;
    bool l3_mbm_total;
    bool l3_mbm_local;
    bool non_cpu_l3_cmt;
    bool non_cpu_l3_mbm;
    bool overflow_bit;
    u32 counter_width;
  } cap;
  u32 scaling_factor;
  u32 max_rmid;
};

#endif /* _MBM_TYPES_H_ */
