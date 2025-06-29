#include "uncore_pmu.h"
#include "cpucounters.h"
#include "mmio.h"
#include "types.h"
#include "uncore_pmu_discovery.h"

uncore_pmus_t g_uncore_pmus = {
    .num_imcs = 0,
};

static __always_inline hw_reg_t*
make_register(const u64 raw_addr, const u32 bits) {
    const size_t map_size = SERVER_MC_CH_PMON_SIZE;
    const size_t aligned_addr = raw_addr & ~4095ULL;
    const size_t align_delta = raw_addr & 4095ULL;
    pr_debug("kimc: raw_addr: 0x%llx, aligned_addr: 0x%zx, align_delta: 0x%zx\n",
            raw_addr, aligned_addr, align_delta);

    mmio_range_t* handle = mmio_range_create(aligned_addr, map_size, false, -1);
    if (handle == NULL) {
        pr_err("kimc: Failed to create MMIO range\n");
        return NULL;
    }

    if (IS_ERR(handle)) {
        pr_err("kimc: Failed to create MMIO range\n");
        return NULL;
    }

    if (bits == 32) {
        return mmio_reg32_create(handle, align_delta);
    } else if (bits == 64) {
        return mmio_reg64_create(handle, align_delta);
    } else {
        pr_err("kimc: Unsupported register bit width: %u\n", bits);
        mmio_range_destroy(handle);
        return NULL;
    }
}

static bool
program_counter_with_config(uncore_pmu_t* pmu, const u32* conf,
                            const u32 conf_len, const u32 extra) {
    if (!pmu) {
        pr_err("kimc: program_counter_with_config: pmu is NULL\n");
        return false;
    }
    const u32 num_cters = uncore_pmu_get_size(pmu);
    for (u32 i = 0; i < num_cters && i < conf_len; i++) {
        if (pmu->counter_ctrl[i]) {
            hw_reg_write(pmu->counter_ctrl[i], conf[i]);
        } else {
            pr_err(
                "kimc: program_counter_with_config: counter_ctrl[%u] is NULL\n",
                i);
            return false;
        }
    }

    if (extra) {
        bool ok = uncore_pmu_reset_unfreeze(pmu, extra);
        if (!ok) {
            pr_err("kimc: program_counter_with_config: Failed to reset and "
                   "unfreeze PMU\n");
            return false;
        }
    }

    return true;
}

static int
program_imc(const u32* mccnt_conf) {
    // Program the IMC PMU with the provided configuration
    const u32 extra_imc = UNC_PMON_UNIT_CTL_FRZ_EN;
    const u32 max_imcs = g_uncore_pmus.num_imcs;
    for (u32 i = 0; i < max_imcs; i++) {
        bool ok = uncore_pmu_init_freeze(g_uncore_pmus.imcs[i]);
        if (!ok) {
            pr_err("kimc: Failed to freeze IMC PMU %u\n", i);
            return -EINVAL;
        }
        ok =
            uncore_pmu_enable_and_reset_mc_fixed_counter(g_uncore_pmus.imcs[i]);
        if (!ok) {
            pr_err("kimc: Failed to enable and reset fixed counter for IMC PMU "
                   "%u\n",
                   i);
            return -EINVAL;
        }
        ok = program_counter_with_config(g_uncore_pmus.imcs[i], mccnt_conf, 4,
                                         extra_imc);
        if (!ok) {
            pr_err("kimc: Failed to program counters for IMC PMU %u\n", i);
            return -EINVAL;
        }
    }
    return 0;
}

static int
program_counters(void) {
    u32 mccnt_conf[4] = {0, 0, 0, 0};

    if (CPU_MODEL == SPR) {
        mccnt_conf[EVENT_READ] =
            MC_CH_PCI_PMON_CTL_EVENT(0x05)
            + MC_CH_PCI_PMON_CTL_UMASK(
                0xcf); // monitor reads on counter 0: CAS_COUNT.RD
        mccnt_conf[EVENT_WRITE] =
            MC_CH_PCI_PMON_CTL_EVENT(0x05)
            + MC_CH_PCI_PMON_CTL_UMASK(
                0xf0); // monitor writes on counter 1: CAS_COUNT.WR

        // For now, we set register 2 and 3 to PMM_READ and PMM_WRITE events, respectively.
        mccnt_conf[EVENT_PMM_READ] = MC_CH_PCI_PMON_CTL_EVENT(
            0xe3); // monitor PMM_RDQ_REQUESTS on counter 2
        mccnt_conf[EVENT_PMM_WRITE] = MC_CH_PCI_PMON_CTL_EVENT(
            0xe7); // monitor PMM_WPQ_REQUESTS on counter 3
    } else {
        pr_err("kimc: Unsupported CPU model for counter initialization\n");
        return -EINVAL;
    }

    int ok = program_imc(mccnt_conf);
    if (ok != 0) {
        pr_err("kimc: Failed to program IMC counters\n");
        return ok;
    }
    return 0;
}

int
init_g_uncore_pmus(void) {
    g_uncore_pmus.num_imcs = 0;
    memset(g_uncore_pmus.imcs, 0, sizeof(g_uncore_pmus.imcs));

    g_uncore_pmus.discovery = uncore_pmu_discovery_create();

    if (IS_ERR(g_uncore_pmus.discovery)) {
        pr_err("kimc: Failed to create uncore PMU discovery structure\n");
        g_uncore_pmus.discovery = NULL;
        return -EINVAL;
    } else {
        pr_info("kimc: Uncore PMU discovery enabled successfully\n");
    }

    // Initialize the IMC PMUs based on the discovery structure
    // assume SPR (Sapphire Rapid) CPU family for now...
    const u32 BOX_TYPE = SPR_IMC_BOX_TYPE;
    // We don't support >1 socket systems yet, assuming it's a single socket system.
    pr_debug("kimc: BOX_TYPE: %u\n", BOX_TYPE);
    const size_t num_boxes =
        get_num_boxes(g_uncore_pmus.discovery, BOX_TYPE, SOCKET_ID);
    pr_debug("kimc: Number of boxes: %zu\n", num_boxes);
    for (size_t pos = 0; pos < num_boxes; pos++) {
        if (get_box_access_type(g_uncore_pmus.discovery, BOX_TYPE, SOCKET_ID,
                                pos)
            == ACCESS_TYPE_MMIO) {
            // Initialize the IMC PMU for this box
            hw_reg_t* ctrl_regs[MAX_HW_REGS_PER_IMC_PMU] = {0};
            hw_reg_t* val_regs[MAX_HW_REGS_PER_IMC_PMU] = {0};
            const size_t n_regs = get_box_num_regs(g_uncore_pmus.discovery,
                                                   BOX_TYPE, SOCKET_ID, pos);

            if (n_regs > MAX_HW_REGS_PER_IMC_PMU) {
                pr_err("kimc: Too many registers for IMC PMU at pos %zu\n",
                       pos);
                return -EINVAL;
            }

            pr_debug("kimc: box_type: %u, socket: %u, pos: %zu, n_regs: %zu\n",
                    BOX_TYPE, SOCKET_ID, pos, n_regs);

            hw_reg_t* box_ctl_reg =
                make_register(get_box_ctl_addr(g_uncore_pmus.discovery,
                                               BOX_TYPE, SOCKET_ID, pos),
                              32);
            // pr_info("kimc: box_ctl_reg: %p\n", box_ctl_reg);
            // const mmio_reg32_t* box_ctl_reg_mmio =
            //     (const mmio_reg32_t*)box_ctl_reg;
            // pr_info("kimc: box_ctl_reg_mmio->offset: %zu\n",
            //         box_ctl_reg_mmio->offset);
            // pr_info("kimc: box_ctl_reg_mmio->magic: %s\n",
            //         box_ctl_reg_mmio->hw_reg.magic);
            // pr_info("kimc: box_ctl_reg_mmio->ops: %p\n",
            //         box_ctl_reg_mmio->hw_reg.ops);
            // pr_info("kimc: box_ctl_reg_mmio->ops->read: %p\n",
            //         box_ctl_reg_mmio->hw_reg.ops->read);
            // pr_info("kimc: box_ctl_reg_mmio->ops->write: %p\n",
            //         box_ctl_reg_mmio->hw_reg.ops->write);

            if (box_ctl_reg != NULL) {
                for (size_t r = 0; r < n_regs; r++) {
                    ctrl_regs[r] =
                        make_register(get_box_ctl_addr_with_counter(
                                          g_uncore_pmus.discovery, BOX_TYPE,
                                          SOCKET_ID, pos, r),
                                      32);
                    val_regs[r] = make_register(
                        get_box_ctr_addr(g_uncore_pmus.discovery, BOX_TYPE,
                                         SOCKET_ID, pos, r),
                        64);
                }

                hw_reg_t* filter_regs[2] = {NULL, NULL};
                g_uncore_pmus.imcs[pos] = uncore_pmu_create(
                    box_ctl_reg, ctrl_regs, val_regs,
                    make_register(get_box_ctl_addr(g_uncore_pmus.discovery,
                                                   BOX_TYPE, SOCKET_ID, pos)
                                      + SERVER_MC_CH_PMON_FIXED_CTL_OFFSET,
                                  32),
                    make_register(get_box_ctl_addr(g_uncore_pmus.discovery,
                                                   BOX_TYPE, SOCKET_ID, pos)
                                      + SERVER_MC_CH_PMON_FIXED_CTR_OFFSET,
                                  64),
                    filter_regs);
                g_uncore_pmus.num_imcs++;
            }
        }
    }

    int result = program_counters();
    if (result != 0) {
        return result;
    }
    return 0;
}

void
destroy_g_uncore_pmus(void) {
    if (g_uncore_pmus.discovery) {
        uncore_pmu_discovery_destroy(g_uncore_pmus.discovery);
        g_uncore_pmus.discovery = NULL;
    }
    g_uncore_pmus.num_imcs = 0;
    memset(g_uncore_pmus.imcs, 0, sizeof(g_uncore_pmus.imcs));
}
