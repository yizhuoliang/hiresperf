#include "cpucounters.h"

void
mmio_reg32_destroy(hw_reg_t* self) {
    mmio_reg32_t* reg = (mmio_reg32_t*)self;
    if (reg->handle) {
        mmio_range_destroy(reg->handle);
    }
    kfree(reg);
}

hw_reg_t*
mmio_reg32_create(mmio_range_t* const handle, const size_t offset) {
    mmio_reg32_t* reg = kmalloc(sizeof(mmio_reg32_t), GFP_KERNEL);
    if (!reg) {
        pr_err("kimc: Failed to allocate memory for mmio_reg32_t\n");
        return NULL;
    }
    reg->hw_reg.magic = (char*)HW_REG_MAGIC;
    reg->hw_reg.ops = &vt_mmio_reg32_ops;
    reg->handle = handle;
    reg->offset = offset;
    return (hw_reg_t*)reg;
}

void
mmio_reg64_destroy(hw_reg_t* self) {
    mmio_reg64_t* reg = (mmio_reg64_t*)self;
    if (reg->handle) {
        mmio_range_destroy(reg->handle);
    }
    kfree(reg);
}

hw_reg_t*
mmio_reg64_create(mmio_range_t* const handle, const u64 offset) {
    mmio_reg64_t* reg = kmalloc(sizeof(mmio_reg64_t), GFP_KERNEL);
    if (!reg) {
        pr_err("kimc: Failed to allocate memory for mmio_reg64_t\n");
        return NULL;
    }
    reg->hw_reg.magic = (char*)HW_REG_MAGIC;
    reg->hw_reg.ops = &vt_mmio_reg64_ops;
    reg->handle = handle;
    reg->offset = offset;
    return (hw_reg_t*)reg;
}

void
hw_reg_destroy(hw_reg_t* self) {
    if (!hw_reg_is_valid(self)) {
        return;
    }
    if (self && self->ops && self->ops->destroy) {
        self->ops->destroy(self);
    } else {
        pr_err("kimc: hw_reg_destroy called with NULL or invalid ops\n");
    }
}