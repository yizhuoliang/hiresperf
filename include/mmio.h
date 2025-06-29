#ifndef MMIO_H
#define MMIO_H

#include <linux/io.h>
#include <linux/ioport.h>
#include <linux/slab.h>
#include <linux/types.h>
#include <linux/mm.h>
#include <linux/errno.h>

typedef void __iomem mmio_addr_t;

/* MMIO Range structure for memory-mapped I/O operations */
typedef struct mmio_range {
    mmio_addr_t* mmap_addr; /* Memory mapped address */
    u64 size;               /* Size of the mapped region */
    bool silent;            /* Silent operation flag */
    int core;               /* CPU core affinity */

    // struct resource* mem_region_res;
} mmio_range_t;

/* Function prototypes for MMIO operations */
mmio_range_t* mmio_range_create(const u64 base_addr, const u64 size,
                                const bool silent, const int core);
void mmio_range_destroy(mmio_range_t* range);

static __always_inline u32
mmio_read32(const mmio_range_t* range, u64 offset) {
    // TODO: core affinity
    if (offset + sizeof(u32) > range->size) {
        pr_err("kimc: MMIO read32 out of bounds at offset %llu\n", offset);
        return 0;
    }
    return ioread32(range->mmap_addr + offset);
}

static __always_inline u64
mmio_read64(const mmio_range_t* range, u64 offset) {
    // TODO: core affinity
    if (offset + sizeof(u64) > range->size) {
        pr_err("kimc: MMIO read64 out of bounds at offset %llu\n", offset);
        return 0;
    }
    return ioread64_lo_hi(range->mmap_addr + offset);
}

static __always_inline void
mmio_write32(mmio_range_t* range, u64 offset, u32 val) {
    // TODO: core affinity
    if (offset + sizeof(u32) > range->size) {
        pr_err("kimc: MMIO write32 out of bounds at offset %llu\n", offset);
        return;
    }
    iowrite32(val, range->mmap_addr + offset);
}

static __always_inline void
mmio_write64(mmio_range_t* range, u64 offset, u64 val) {
    // TODO: core affinity
    if (offset + sizeof(u64) > range->size) {
        pr_err("kimc: MMIO write64 out of bounds at offset %llu\n", offset);
        return;
    }
    iowrite64_lo_hi(val, range->mmap_addr + offset);
}

static __always_inline int
mmio_memcpy(void* dest, const u64 src, const size_t n,
            const bool check_failures, const bool silent)
{
    u64 i;
    u32* d = (u32*)dest;
    mmio_range_t* range;
    u64 map_begin, map_size;

    if (WARN_ON((src & (sizeof(u32) - 1)) != 0) ||
        WARN_ON((n & (sizeof(u32) - 1)) != 0)) {
        return -EINVAL;
    }

    map_begin = src & PAGE_MASK;
    map_size = PAGE_ALIGN(src + n) - map_begin;

    // core is -1 to indicate any core
    range = mmio_range_create(map_begin, map_size, silent, -1);
    if (!range) {
        if (!silent) {
            pr_err("kimc: mmio_memcpy failed to create range for addr 0x%llx size 0x%llx\n", map_begin, map_size);
        }
        return -ENOMEM;
    }

    for (i = 0; i < n; i += sizeof(u32)) {
        const u32 value = mmio_read32(range, src - map_begin + i);
        if (check_failures && value == U32_MAX) {
            if (!silent) {
                pr_err("kimc: Failed to read memory at 0x%llx\n", src + i);
            }
            mmio_range_destroy(range);
            return -EIO;
        }
        *d++ = value;
    }

    mmio_range_destroy(range);
    return 0;
}

#endif // MMIO_H
