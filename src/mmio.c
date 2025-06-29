#include "mmio.h"

mmio_range_t*
mmio_range_create(const u64 base_addr, const u64 size, const bool silent,
                  const int core) {
    mmio_range_t* range = kmalloc(sizeof(mmio_range_t), GFP_KERNEL);
    if (!range) {
        return NULL;
    }

    range->mmap_addr = NULL;
    range->size = size;
    range->silent = silent;
    range->core = core;
    
    // pr_info("kimc: mmio_range_create: base_addr: 0x%llx, size: 0x%llu, silent: %d, core: %d\n", base_addr, size, silent, core);

    // char* name = kasprintf(GFP_KERNEL, "mmio_range_%llx", base_addr);
    // range->mem_region_res = request_mem_region(base_addr, size, name);
    // if (IS_ERR(range->mem_region_res) || range->mem_region_res == NULL) {
    //     kfree(name);
    //     // if (!silent) {

    //         pr_err("kimc: Failed to request memory region for MMIO range at "
    //                "%llx\n",
    //                base_addr);
    //     // }
    //     goto mmio_clean;
    // }
    // pr_info("kimc: range->mem_region_res: %p\n", range->mem_region_res);
    // pr_info("kimc: range->mem_region_res->start: %llx\n", range->mem_region_res->start);
    // pr_info("kimc: range->mem_region_res->end: %llx\n", range->mem_region_res->end);
    // pr_info("kimc: range->mem_region_res->size: %llu\n", range->mem_region_res->end - range->mem_region_res->start + 1);

    // assert: requested memory region starts at the given base address
    // if (range->mem_region_res->start != base_addr) {
    //     // if (!silent) {
    //         pr_err("kimc: Memory region start address %llx does not match "
    //                "requested base address %llx\n",
    //                range->mem_region_res->start, base_addr);
    //     // }
    //     goto mmio_clean;
    // }

    // range->mmap_addr = ioremap(range->mem_region_res->start, size);
    range->mmap_addr = ioremap(base_addr, size);
    if (IS_ERR(range->mmap_addr) || range->mmap_addr == NULL) {
        // if (!silent) {
            pr_err("kimc: ioremap failed. Error code: %ld\n",
                   PTR_ERR(range->mmap_addr));
        // }
        goto mmio_clean;
    }
    // pr_info("kimc: range->mmap_addr: %p\n", range->mmap_addr);

    return range;

mmio_clean:
    if (range->mmap_addr) {
        iounmap(range->mmap_addr);
    }

    // if (range->mem_region_res) {
    //     release_mem_region(range->mem_region_res->start, size);
    //     range->mem_region_res = NULL;
    // }
    // pr_info("kimc: cleaned mem_region_res\n");
    if (range) {
        kfree(range);
    }
    return NULL;
}

void mmio_range_destroy(mmio_range_t *range) {
    if (!range) {
        return;
    }

    if (range->mmap_addr) {
        iounmap(range->mmap_addr);
    }

    // if (range->mem_region_res) {
    //     release_mem_region(range->mem_region_res->start, range->size);
    // }

    kfree(range);
}
