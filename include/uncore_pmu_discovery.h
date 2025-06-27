#ifndef UNCORE_PMU_DISCOVERY_H
#define UNCORE_PMU_DISCOVERY_H

#include <linux/cpu.h>
#include <linux/list.h>
#include <linux/types.h>

static const u32 SPR_PCU_BOX_TYPE = 4U;
static const u32 SPR_IMC_BOX_TYPE = 6U;
static const u32 SPR_UPILL_BOX_TYPE = 8U;
static const u32 SPR_MDF_BOX_TYPE = 11U;
static const u32 SPR_CXLCM_BOX_TYPE = 12U;
static const u32 SPR_CXLDP_BOX_TYPE = 13U;

// const u32 BHS_MDF_BOX_TYPE = 20U;
// const u32 BHS_PCIE_GEN5x16_TYPE = 21U;
// const u32 BHS_PCIE_GEN5x8_TYPE = 22U;

// For now, only MMIO is ported.
enum access_type_enum {
    ACCESS_TYPE_MSR = 0,
    ACCESS_TYPE_MMIO = 1,
    ACCESS_TYPE_PCICFG = 2,
    ACCESS_TYPE_UNKNOWN = 255
};

union pci_cfg_address {
    u64 raw;

    struct {
        u64 offset     : 12;
        u64 function   : 3;
        u64 device     : 5;
        u64 bus        : 8;
        u64 __reserved : 36;
    } fields;
};

struct global_pmu {
    u64 type        : 8;
    u64 stride      : 8;
    u64 max_units   : 10;
    u64 __reserved1 : 36;
    u64 access_type : 2;
    u64 global_ctrl_addr;
    u64 status_offset : 8;
    u64 num_status    : 16;
    u64 __reserved2   : 40;
};

struct box_pmu {
    u64 num_regs      : 8;
    u64 ctrl_offset   : 8;
    u64 bit_width     : 8;
    u64 ctr_offset    : 8;
    u64 status_offset : 8;
    u64 __reserved1   : 22;
    u64 access_type   : 2;
    u64 box_ctrl_addr;
    u64 box_type    : 16;
    u64 box_id      : 16;
    u64 __reserved2 : 32;
};

// A list of box PMUs for a specific box type
struct box_pmu_list {
    struct box_pmu* pmus;
    size_t count;
};

// A map from box_type to a list of box_pmus for a socket
#define MAX_BOX_TYPES 32

struct box_pmu_map {
    struct box_pmu_list lists[MAX_BOX_TYPES];
};

typedef struct uncore_pmu_discovery {
    struct box_pmu_map* socket_maps;
    int num_sockets;

    struct global_pmu* global_pmus;
    size_t num_global_pmus;
} uncore_pmu_discovery_t;

static __always_inline bool
valid_box(struct uncore_pmu_discovery* d, size_t socket, size_t box_type,
          size_t pos) {
    if (socket < (size_t)d->num_sockets && box_type < MAX_BOX_TYPES) {
        return pos < d->socket_maps[socket].lists[box_type].count;
    }
    return false;
}

uncore_pmu_discovery_t* uncore_pmu_discovery_create(void);
void uncore_pmu_discovery_destroy(uncore_pmu_discovery_t* d);

static size_t
register_step(uncore_pmu_discovery_t* d, size_t socket, size_t box_type,
              size_t pos) {
    const struct box_pmu* pmu =
        &d->socket_maps[socket].lists[box_type].pmus[pos];
    const u64 width = pmu->bit_width;
    switch (pmu->access_type) {
        case ACCESS_TYPE_MSR:
            if (width <= 64) {
                return 1;
            }
            break;
        case ACCESS_TYPE_PCICFG:
        case ACCESS_TYPE_MMIO:
            if (width <= 8) {
                return 1;
            } else if (width <= 16) {
                return 2;
            } else if (width <= 32) {
                return 4;
            } else if (width <= 64) {
                return 8;
            }
            break;
        default: break;
    }
    return 0;
}

static __always_inline const char*
access_type_to_str(enum access_type_enum t) {
    switch (t) {
        case ACCESS_TYPE_MSR: return "MSR";
        case ACCESS_TYPE_MMIO: return "MMIO";
        case ACCESS_TYPE_PCICFG: return "PCICFG";
        default: return "unknown";
    }
}

static __always_inline int
pci_cfg_address_snprint(char* buf, size_t size,
                        const union pci_cfg_address* addr) {
    return snprintf(buf, size, "%x:%x.%x@%llx", (unsigned int)addr->fields.bus,
                    (unsigned int)addr->fields.device,
                    (unsigned int)addr->fields.function,
                    (unsigned long long)addr->fields.offset);
}

// void print_global_pmu(const struct global_pmu* pmu);
// void print_box_pmu(const struct box_pmu* pmu);

static __always_inline size_t
get_num_boxes(struct uncore_pmu_discovery* d, size_t box_type, size_t socket) {
    if (socket < (size_t)d->num_sockets && box_type < MAX_BOX_TYPES) {
        return d->socket_maps[socket].lists[box_type].count;
    }
    return 0;
}

static __always_inline u64
get_box_ctl_addr(struct uncore_pmu_discovery* d, size_t box_type, size_t socket,
                 size_t pos) {
    if (valid_box(d, socket, box_type, pos)) {
        return d->socket_maps[socket].lists[box_type].pmus[pos].box_ctrl_addr;
    }
    return 0;
}

static __always_inline u64
get_box_ctl_addr_with_counter(struct uncore_pmu_discovery* d, size_t box_type,
                              size_t socket, size_t pos, size_t c) {
    if (valid_box(d, socket, box_type, pos)
        && c < d->socket_maps[socket].lists[box_type].pmus[pos].num_regs) {
        const struct box_pmu* pmu =
            &d->socket_maps[socket].lists[box_type].pmus[pos];
        const size_t step = (box_type == SPR_IMC_BOX_TYPE)
                                ? 4
                                : register_step(d, socket, box_type, pos);
        return pmu->box_ctrl_addr + pmu->ctrl_offset + c * step;
    }
    return 0;
}

static __always_inline u64
get_box_ctr_addr(struct uncore_pmu_discovery* d, size_t box_type, size_t socket,
                 size_t pos, size_t c) {
    if (valid_box(d, socket, box_type, pos)
        && c < d->socket_maps[socket].lists[box_type].pmus[pos].num_regs) {
        const struct box_pmu* pmu =
            &d->socket_maps[socket].lists[box_type].pmus[pos];
        return pmu->box_ctrl_addr + pmu->ctr_offset
               + c * register_step(d, socket, box_type, pos);
    }
    return 0;
}

static __always_inline enum access_type_enum
get_box_access_type(struct uncore_pmu_discovery* d, size_t box_type,
                    size_t socket, size_t pos) {

    if (valid_box(d, socket, box_type, pos)) {
        return (enum access_type_enum)d->socket_maps[socket]
            .lists[box_type]
            .pmus[pos]
            .access_type;
    }
    return ACCESS_TYPE_UNKNOWN;
}

static __always_inline u64
get_box_num_regs(struct uncore_pmu_discovery* d, size_t box_type, size_t socket,
                 size_t pos) {

    if (valid_box(d, socket, box_type, pos)) {
        return d->socket_maps[socket].lists[box_type].pmus[pos].num_regs;
    }
    return 0;
}

#endif // UNCORE_PMU_DISCOVERY_H