#ifndef _MBM_COUNTER_H_
#define _MBM_COUNTER_H_

#include "mbm/types.h"

extern struct mbm_manager g_mbm_mgr;

typedef enum mbm_counter_error {
    MBM_COUNTER_READ_SUCCESS = 0,
    MBM_COUNTER_READ_ERROR,
    MBM_COUNTER_READ_UNAVAIL,
    MBM_COUNTER_READ_INVALID,
} mbm_counter_error_t;

typedef struct mbm_counter_data {
    u32 rmid;
    u64 total_bw;
    u64 local_bw;
    u64 occupancy;
    mbm_counter_error_t status;
} mbm_counter_data_t;

/* Read a specific MBM event for a given RMID */
static __always_inline mbm_counter_error_t mbm_read_event(u32 rmid, u32 event, u64* data) {
    if (event != MBM_EVENT_L3_OCCUP &&
        event != MBM_EVENT_L3_TOTAL_BW &&
        event != MBM_EVENT_L3_LOCAL_BW) {
        return MBM_COUNTER_READ_INVALID;
    }

    u64 val;
    u64 val_evtsel = 0;

    val_evtsel = ((u64)rmid) & QM_EVTSEL_RMID_MASK;
    val_evtsel <<= QM_EVTSEL_RMID_SHIFT;
    val_evtsel |= ((u64)event) & QM_EVTSEL_EVENT_MASK;

    wrmsr(MSR_IA32_QM_EVTSEL, LOWER_32(val_evtsel), UPPER_32(val_evtsel));
    rdmsrl(MSR_IA32_QM_CTR, val);

    if (val & QM_CTR_ERROR_MASK) {
        return MBM_COUNTER_READ_ERROR;
    }

    if (val & QM_CTR_UNAVAIL_MASK) {
        return MBM_COUNTER_READ_UNAVAIL;
    }

    return val & QM_CTR_DATA_MASK;
}

/* Read all MBM counters for a given RMID and store in mbm_counter_data_t */
static __always_inline void mbm_read_counters(void* info) {
    mbm_counter_data_t* data = (mbm_counter_data_t*)info;
    mbm_counter_error_t err;

    data->status = MBM_COUNTER_READ_SUCCESS;

    err = mbm_read_event(data->rmid, MBM_EVENT_L3_TOTAL_BW, &data->total_bw);
    if (err != MBM_COUNTER_READ_SUCCESS) {
        data->status = err;
        return;
    }

    err = mbm_read_event(data->rmid, MBM_EVENT_L3_LOCAL_BW, &data->local_bw);
    if (err != MBM_COUNTER_READ_SUCCESS) {
        data->status = err;
        return;
    }

    err = mbm_read_event(data->rmid, MBM_EVENT_L3_OCCUP, &data->occupancy);
    if (err != MBM_COUNTER_READ_SUCCESS) {
        data->status = err;
        return;
    }
}

static __always_inline void mbm_guard_enter(void) {
    struct rmid_manager *g_rmid_mgr = &g_mbm_mgr.rmid_mgr;
    spin_lock(&g_rmid_mgr->lock);
}

static __always_inline void mbm_guard_exit(void) {
    struct rmid_manager *g_rmid_mgr = &g_mbm_mgr.rmid_mgr;
    spin_unlock(&g_rmid_mgr->lock);
}

static __always_inline void mbm_poll_all_cores(void) {
    struct rmid_manager *g_rmid_mgr = &g_mbm_mgr.rmid_mgr;
    for (int idx= 0; idx < MAX_CORES; idx++) {
        struct rmid_info *info = &g_rmid_mgr->rmid_table[idx];
        if (info->in_use) {
            mbm_counter_data_t data;
            data.rmid = info->rmid;
            mbm_read_counters(&data);
            if (data.status == MBM_COUNTER_READ_SUCCESS) {
                // pr_info("Core %d RMID %u: Total BW: %llu, Local BW: %llu, Occupancy: %llu\n",
                //         info->core_id, data.rmid, data.total_bw, data.local_bw, data.occupancy);
                
                info->last_local_bw = info->new_local_bw;
                info->last_total_bw = info->new_total_bw;
                info->last_occupancy = info->new_occupancy;

                info->new_local_bw = data.local_bw;
                info->new_total_bw = data.total_bw;
                info->new_occupancy = data.occupancy;
            } else {
                pr_warn("Core %d RMID %u: Failed to read counters, status: %d\n",
                        info->core_id, data.rmid, data.status);
            }
        }
        // if (info->in_use) {
        //     mbm_counter_data_t data;
        //     data.rmid = info->rmid;
        //     mbm_read_counters(&data);
        //     if (data.status == MBM_COUNTER_READ_SUCCESS) {
        //         pr_info("Core %d RMID %u: Total BW: %llu, Local BW: %llu, Occupancy: %llu\n",
        //                 cpu, data.rmid, data.total_bw, data.local_bw, data.occupancy);
        //     } else {
        //         pr_warn("Core %d RMID %u: Failed to read counters, status: %d\n",
        //                 cpu, data.rmid, data.status);
        //     }
        // }
    }
}

static __always_inline struct rmid_info* mbm_get_rmid_info_for_core(u32 core_id) {
    struct rmid_manager *g_rmid_mgr = &g_mbm_mgr.rmid_mgr;
    if (core_id >= MAX_CORES) {
        return NULL;
    }
    u8 idx = g_rmid_mgr->core_to_rmid_map[core_id];
    if (idx >= MAX_CORES) {
        return NULL;
    }
    struct rmid_info *info = &g_rmid_mgr->rmid_table[idx];
    if (info->in_use && info->core_id == core_id) {
        return info;
    }
    return NULL;
}

#endif /* _MBM_COUNTER_H_ */