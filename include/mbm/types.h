#ifndef _MBM_TYPES_H_
#define _MBM_TYPES_H_

#include <asm/msr-index.h>
#include <linux/spinlock.h>
#include <linux/types.h>

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
