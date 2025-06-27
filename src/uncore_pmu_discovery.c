#include <linux/cpu.h>
#include <linux/device.h>
#include <linux/pci.h>
#include <linux/slab.h>
#include <linux/smp.h>

#include "mmio.h"
#include "pci.h"
#include "uncore_pmu_discovery.h"

static int
process_socket_discovery_table(struct uncore_pmu_discovery* d, int socket,
                               u64 discovery_table_addr) {
    union {
        struct global_pmu pmu;
        u64 table[3];
    } global;

    int ret;
    size_t u;
    u64 step;
    size_t counts[MAX_BOX_TYPES] = {0};
    int i;

    ret = mmio_memcpy(&global.table, discovery_table_addr, sizeof(global.table),
                      true, false);
    if (ret) {
        pr_err("kimc: failed to read global pmu for socket %d\n", socket);
        return ret;
    }
    memcpy(&d->global_pmus[socket], &global.pmu, sizeof(struct global_pmu));

    step = global.pmu.stride * 8;

    // First pass: count box PMUs per type
    for (u = 0; u < global.pmu.max_units; ++u) {
        union {
            struct box_pmu pmu;
            u64 table[3];
        } unit;

        ret = mmio_memcpy(&unit.table, discovery_table_addr + (u + 1) * step,
                          sizeof(unit.table), true, false);
        if (ret) {
            pr_warn("kimc: failed to read box pmu %zu for socket %d\n", u,
                    socket);
            continue;
        }
        if (unit.table[0] == 0 && unit.table[1] == 0) {
            continue;
        }
        if (unit.pmu.box_type < MAX_BOX_TYPES) {
            counts[unit.pmu.box_type]++;
        }
    }

    // Allocate memory
    for (i = 0; i < MAX_BOX_TYPES; ++i) {
        if (counts[i] > 0) {
            struct box_pmu_list* list = &d->socket_maps[socket].lists[i];
            list->pmus = kcalloc(counts[i], sizeof(struct box_pmu), GFP_KERNEL);
            if (!list->pmus) {
                pr_err("kimc: failed to allocate memory for box pmus\n");
                return -ENOMEM;
            }
            list->count = 0; // Will be used as index
        }
    }

    // Second pass: fill data
    for (u = 0; u < global.pmu.max_units; ++u) {
        union {
            struct box_pmu pmu;
            u64 table[3];
        } unit;

        ret = mmio_memcpy(&unit.table, discovery_table_addr + (u + 1) * step,
                          sizeof(unit.table), true, false);
        if (ret) {
            continue; // already warned
        }
        if (unit.table[0] == 0 && unit.table[1] == 0) {
            continue;
        }
        if (unit.pmu.box_type < MAX_BOX_TYPES) {
            struct box_pmu_list* list =
                &d->socket_maps[socket].lists[unit.pmu.box_type];
            if (list->pmus && list->count < counts[unit.pmu.box_type]) {
                memcpy(&list->pmus[list->count], &unit.pmu,
                       sizeof(struct box_pmu));
                list->count++;
            }
        }
    }

    return 0;
}

static bool
match_function(const union pcm_vsec* vsec) {
    return vsec->fields.cap_id == 0x23     // UNCORE_EXT_CAP_ID_DISCOVERY
           && vsec->fields.entryID == 0x1; // UNCORE_DISCOVERY_DVSEC_ID_PMON
}

static void
process_dvsec_callback(struct pci_dev* dev, u64 bar, const union pcm_vsec* vsec,
                       void* priv) {
    struct uncore_pmu_discovery* d = priv;
    int socket = dev_to_node(&dev->dev);
    if (socket < 0 || socket >= d->num_sockets) {
        pr_warn("kimc: invalid socket %d for dev %s\n", socket, pci_name(dev));
        return;
    }
    if (process_socket_discovery_table(d, socket, bar) != 0) {
        pr_err("kimc: failed to process discovery table for socket %d\n",
               socket);
    }
}

uncore_pmu_discovery_t*
uncore_pmu_discovery_create(void) {
    struct uncore_pmu_discovery* d;
    
    d = kzalloc(sizeof(uncore_pmu_discovery_t), GFP_KERNEL);
    if (!d)
        return ERR_PTR(-ENOMEM);

    d->num_sockets = num_online_nodes();
    d->socket_maps =
        kcalloc(d->num_sockets, sizeof(*d->socket_maps), GFP_KERNEL);
    if (!d->socket_maps) {
        kfree(d);
        return ERR_PTR(-ENOMEM);
    }
    d->global_pmus =
        kcalloc(d->num_sockets, sizeof(*d->global_pmus), GFP_KERNEL);
    if (!d->global_pmus) {
        kfree(d->socket_maps);
        kfree(d);
        return ERR_PTR(-ENOMEM);
    }
    
    pcm_process_dvsec(match_function, process_dvsec_callback, d);

    return d;
}

void
uncore_pmu_discovery_destroy(uncore_pmu_discovery_t* d) {
    int i, j;
    if (d->socket_maps) {
        for (i = 0; i < d->num_sockets; ++i) {
            for (j = 0; j < MAX_BOX_TYPES; ++j) {
                kfree(d->socket_maps[i].lists[j].pmus);
            }
        }
        kfree(d->socket_maps);
    }
    kfree(d->global_pmus);
    kfree(d);
}
