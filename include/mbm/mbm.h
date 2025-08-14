#ifndef _MBM_H_
#define _MBM_H_

#include "mbm/types.h"

extern mbm_manager_t g_mbm_mgr;

int mbm_init(void);
void mbm_init_cap(void);
int mbm_deinit(void);

static __always_inline u32 mbm_get_scaling_factor(void) {
    return g_mbm_mgr.scaling_factor; 
}

static __always_inline u32 mbm_get_max_rmid(void) {
    return g_mbm_mgr.max_rmid;
}

#endif /* _MBM_H_ */