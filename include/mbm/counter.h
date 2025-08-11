#ifndef _MBM_COUNTER_H_
#define _MBM_COUNTER_H_

#include "mbm/types.h"

typedef enum mbm_counter_error {
    MBM_COUNTER_SUCCESS = 0,
    MBM_COUNTER_ERROR,
    MBM_COUNTER_UNAVAIL,
    MBM_COUNTER_INVALID,
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
        return MBM_COUNTER_INVALID;
    }

    u64 val;
    u64 val_evtsel = 0;

    val_evtsel = ((u64)rmid) & QM_EVTSEL_RMID_MASK;
    val_evtsel <<= QM_EVTSEL_RMID_SHIFT;
    val_evtsel |= ((u64)event) & QM_EVTSEL_EVENT_MASK;

    wrmsr(MSR_IA32_QM_EVTSEL, LOWER_32(val_evtsel), UPPER_32(val_evtsel));
    rdmsrl(MSR_IA32_QM_CTR, val);

    if (val & QM_CTR_ERROR_MASK) {
        return MBM_COUNTER_ERROR;
    }

    if (val & QM_CTR_UNAVAIL_MASK) {
        return MBM_COUNTER_UNAVAIL;
    }

    return val & QM_CTR_DATA_MASK;
}

/* Read all MBM counters for a given RMID and store in mbm_counter_data_t */
static __always_inline void mbm_read_counters(void* info) {
    mbm_counter_data_t* data = (mbm_counter_data_t*)info;
    mbm_counter_error_t err;

    data->status = MBM_COUNTER_SUCCESS;

    err = mbm_read_event(data->rmid, MBM_EVENT_L3_TOTAL_BW, &data->total_bw);
    if (err != MBM_COUNTER_SUCCESS) {
        data->status = err;
        return;
    }

    err = mbm_read_event(data->rmid, MBM_EVENT_L3_LOCAL_BW, &data->local_bw);
    if (err != MBM_COUNTER_SUCCESS) {
        data->status = err;
        return;
    }

    err = mbm_read_event(data->rmid, MBM_EVENT_L3_OCCUP, &data->occupancy);
    if (err != MBM_COUNTER_SUCCESS) {
        data->status = err;
        return;
    }
}

#endif /* _MBM_COUNTER_H_ */