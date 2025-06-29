#ifndef UNCORE_PMU_H
#define UNCORE_PMU_H

#include <linux/delay.h>
#include <linux/kthread.h>

#include "cpucounters.h"
#include "types.h"
#include "uncore_pmu_discovery.h"

#define MAX_IMC_PMUS            (1 << 5) // 32
#define MAX_HW_REGS_PER_IMC_PMU (1 << 4) // 16

static const u32 NUM_SOCKETS = 1; // For now, we assume a single socket system
#define SOCKET_ID 0               // The default socket ID is 0.
#define CPU_MODEL SPR             // Sapphire Rapids

typedef struct uncore_pmu {
    hw_reg_t* unit_ctrl;

    hw_reg_t* counter_ctrl[MAX_HW_REGS_PER_IMC_PMU];
    hw_reg_t* counter_val[MAX_HW_REGS_PER_IMC_PMU];
    hw_reg_t* fixed_counter_ctrl;
    hw_reg_t* fixed_counter_val;
    hw_reg_t* filter[2];
} uncore_pmu_t;

static __always_inline uncore_pmu_t*
uncore_pmu_create(hw_reg_t* unit_ctrl,
                  hw_reg_t* counter_ctrl[MAX_HW_REGS_PER_IMC_PMU],
                  hw_reg_t* counter_val[MAX_HW_REGS_PER_IMC_PMU],
                  hw_reg_t* fixed_counter_ctrl, hw_reg_t* fixed_counter_val,
                  hw_reg_t* filter[2]) {

    uncore_pmu_t* pmu =
        (uncore_pmu_t*)kzalloc(sizeof(uncore_pmu_t), GFP_KERNEL);
    if (!pmu) {
        return NULL;
    }

    pmu->unit_ctrl = unit_ctrl;

    for (int i = 0; i < MAX_HW_REGS_PER_IMC_PMU; i++) {
        pmu->counter_ctrl[i] = counter_ctrl[i];
        pmu->counter_val[i] = counter_val[i];
    }

    pmu->fixed_counter_ctrl = fixed_counter_ctrl;
    pmu->fixed_counter_val = fixed_counter_val;

    pmu->filter[0] = filter[0];
    pmu->filter[1] = filter[1];

    return pmu;
}

static __always_inline bool
uncore_pmu_is_valid(const uncore_pmu_t* pmu) {
    return pmu->unit_ctrl != NULL && hw_reg_is_valid(pmu->unit_ctrl);
}

static __always_inline bool
uncore_pmu_init_freeze(uncore_pmu_t* pmu) {
    if (!uncore_pmu_is_valid(pmu)) {
        return false; // invalid PMU
    }

    if (CPU_MODEL == SPR) {
        hw_reg_write(pmu->unit_ctrl, SPR_UNC_PMON_UNIT_CTL_FRZ);
        hw_reg_write(pmu->unit_ctrl, SPR_UNC_PMON_UNIT_CTL_FRZ
                                         + SPR_UNC_PMON_UNIT_CTL_RST_CONTROL);
    } else {
        pr_err("kimc: uncore_pmu_init_freeze: Unsupported CPU model\n");
        return false;
    }
    return true;
}

static __always_inline bool
uncore_pmu_reset_unfreeze(uncore_pmu_t* pmu, const u32 extra) {
    if (!uncore_pmu_is_valid(pmu)) {
        return false; // invalid PMU
    }

    if (CPU_MODEL == SPR) {
        hw_reg_write(
            pmu->unit_ctrl,
            SPR_UNC_PMON_UNIT_CTL_FRZ
                + SPR_UNC_PMON_UNIT_CTL_RST_COUNTERS); // freeze and reset counter registers
        hw_reg_write(pmu->unit_ctrl, 0);               // unfreeze
        return true;
    } else {
        pr_err("kimc: uncore_pmu_reset_unfreeze: Unsupported CPU model\n");
        return false;
    }
    // currently we don't use 'extra'
    // however, for some CPU models, we may need to set some extra bits in the future.
    return true;
}

static __always_inline bool
uncore_pmu_enable_and_reset_mc_fixed_counter(uncore_pmu_t* pmu) {
    if (!uncore_pmu_is_valid(pmu)) {
        return true; // no-op
    }

    if (pmu->fixed_counter_ctrl) {
        // enable fixed counter (DRAM clocks)
        hw_reg_write(pmu->fixed_counter_ctrl, MC_CH_PCI_PMON_FIXED_CTL_EN);
        // reset it
        hw_reg_write(pmu->fixed_counter_ctrl,
                     MC_CH_PCI_PMON_FIXED_CTL_EN
                         + MC_CH_PCI_PMON_FIXED_CTL_RST);
    } else {
        pr_err("kimc: uncore_pmu_enable_and_reset_mc_fixed_counter: fixed "
               "counter is not initialized.\n");
        return false;
    }
    return true;
}

static __always_inline void
uncore_pmu_freeze(uncore_pmu_t* pmu, const u32 extra) {
    if (!uncore_pmu_is_valid(pmu)) {
        return; // no-op
    }

    if (CPU_MODEL == SPR) {
        hw_reg_write(pmu->unit_ctrl, SPR_UNC_PMON_UNIT_CTL_FRZ);
    } else {
        pr_err("kimc: uncore_pmu_freeze: Unsupported CPU model\n");
        return;
    }
}

static __always_inline void
uncore_pmu_unfreeze(uncore_pmu_t* pmu, const u32 extra) {
    if (!uncore_pmu_is_valid(pmu)) {
        return; // no-op
    }

    if (CPU_MODEL == SPR) {
        hw_reg_write(pmu->unit_ctrl, 0);
    } else {
        pr_err("kimc: uncore_pmu_unfreeze: Unsupported CPU model\n");
        return;
    }
}

static __always_inline u64
uncore_pmu_get_size(const uncore_pmu_t* pmu) {
    u64 count = 0;
    while (count < MAX_HW_REGS_PER_IMC_PMU
           && pmu->counter_ctrl[count] != NULL) {
        count++;
    }
    return count;
}

static __always_inline void
uncore_pmu_destroy(uncore_pmu_t* pmu) {
    if (pmu->unit_ctrl) {
        hw_reg_destroy(pmu->unit_ctrl);
    }
    for (int i = 0; i < MAX_HW_REGS_PER_IMC_PMU; i++) {
        if (pmu->counter_ctrl[i]) {
            hw_reg_destroy(pmu->counter_ctrl[i]);
        }
        if (pmu->counter_val[i]) {
            hw_reg_destroy(pmu->counter_val[i]);
        }
    }
    if (pmu->fixed_counter_ctrl) {
        hw_reg_destroy(pmu->fixed_counter_ctrl);
    }
    if (pmu->fixed_counter_val) {
        hw_reg_destroy(pmu->fixed_counter_val);
    }
    if (pmu->filter[0]) {
        hw_reg_destroy(pmu->filter[0]);
    }
    if (pmu->filter[1]) {
        hw_reg_destroy(pmu->filter[1]);
    }
}

typedef struct uncore_pmus {
    uncore_pmu_t* imcs[MAX_IMC_PMUS];
    u32 num_imcs; // Number of IMC PMUs discovered

    uncore_pmu_discovery_t* discovery;
} uncore_pmus_t;

extern uncore_pmus_t g_uncore_pmus;

int init_g_uncore_pmus(void);
void destroy_g_uncore_pmus(void);

static __always_inline u32
uncore_pmus_get_num_imcs(void) {
    return g_uncore_pmus.num_imcs;
}

static __always_inline void
freeze_all_counters(void) {
    const u32 max_imcs = g_uncore_pmus.num_imcs;
    for (u32 i = 0; i < max_imcs; i++) {
        uncore_pmu_freeze(g_uncore_pmus.imcs[i], 0);
    }
}

static __always_inline void
unfreeze_all_counters(void) {
    const u32 max_imcs = g_uncore_pmus.num_imcs;
    for (u32 i = 0; i < max_imcs; i++) {
        uncore_pmu_unfreeze(g_uncore_pmus.imcs[i], 0);
    }
}

static __always_inline u64
get_mc_counter(u32 channel, u32 counter) {
    if (channel >= MAX_IMC_PMUS || counter >= 4) {
        // the number of per-channel IMC PMU counter is guaranteed to be < 4 (0-3)
        // per Intel's PCM impl.
        pr_err(
            "kimc: get_mc_counter: channel %d or counter %d is out of range\n",
            channel, counter);
        return 0;
    }

    uncore_pmu_t* pmu = g_uncore_pmus.imcs[channel];
    if (!pmu) {
        pr_err("kimc: get_mc_counter: pmu is NULL\n");
        return 0;
    }

    hw_reg_t* cter = pmu->counter_val[counter];
    if (!cter) {
        pr_err("kimc: get_mc_counter: counter is NULL\n");
        return 0;
    }

    return hw_reg_read(cter);
}

static __always_inline u64
get_imc_writes(void) {
    const u32 max_imcs = g_uncore_pmus.num_imcs;
    u64 total_writes = 0;
    for (u32 i = 0; i < max_imcs; ++i) {
        total_writes += get_mc_counter(i, EVENT_WRITE);
        // TODO: for the following CPU families, we also need to add WRITE2 counter.
        //  PCM::GNR:
        //  PCM::GNR_D:
        //  PCM::GRR:
        //  PCM::SRF:
    }
    return total_writes;
}

static __always_inline u64
get_imc_reads(void) {
    const u32 max_imcs = g_uncore_pmus.num_imcs;
    u64 total_reads = 0;
    for (u32 i = 0; i < max_imcs; ++i) {
        total_reads += get_mc_counter(i, EVENT_READ);
        // TODO: for the following CPU families, we also need to add READ2 counter.
        //  PCM::GNR:
        //  PCM::GNR_D:
        //  PCM::GRR:
        //  PCM::SRF:
    }
    return total_reads;
}

static __always_inline void
pr_bw_in_loop(void) {
    const u32 sleep_duration = 1000; // in ms

    u64 before_r = 0;
    u64 before_w = 0;

    u64 after_r = 0;
    u64 after_w = 0;
    while (!kthread_should_stop()) {
        freeze_all_counters();
        after_r = get_imc_reads();
        after_w = get_imc_writes();

        pr_debug("kimc: after_r = %llu, after_w = %llu\n", after_r, after_w);

        u64 delta_r = after_r - before_r;
        u64 delta_w = after_w - before_w;
        u64 denominator;

        if (before_r != 0 && before_w != 0 && sleep_duration > 0) {
            denominator = 10 * (u64)sleep_duration;

            u64 r_bw_scaled = (delta_r * 64) / denominator;
            u64 w_bw_scaled = (delta_w * 64) / denominator;
            u64 bw_scaled = r_bw_scaled + w_bw_scaled;

            pr_info("kimc: Memory BW (R/W/Total): %llu.%02llu / %llu.%02llu / "
                    "%llu.%02llu MB/s\n",
                    r_bw_scaled / 100, r_bw_scaled % 100, w_bw_scaled / 100,
                    w_bw_scaled % 100, bw_scaled / 100, bw_scaled % 100);
        }

        before_r = after_r;
        before_w = after_w;
        unfreeze_all_counters();
        msleep(sleep_duration); // sleep for 1 second
    }
}

#endif // UNCORE_PMU_H