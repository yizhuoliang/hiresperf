#ifndef CPUICOUNTERS_H
#define CPUICOUNTERS_H

#include <linux/types.h>
#include "mmio.h"

#define HW_REG_MAGIC "DEADBEEF\0"

enum EventPosition {
    EVENT_READ = 0,
    EVENT_WRITE = 1,
    EVENT_READ2 = 2,
    EVENT_WRITE2 = 3
};

#define EVENT_READ_RANK_A   EVENT_READ
#define EVENT_WRITE_RANK_A  EVENT_WRITE
#define EVENT_READ_RANK_B   EVENT_READ2
#define EVENT_WRITE_RANK_B  EVENT_WRITE2
#define EVENT_PARTIAL       EVENT_READ2
#define EVENT_PMM_READ      EVENT_READ2
#define EVENT_PMM_WRITE     EVENT_WRITE2
#define EVENT_MM_MISS_CLEAN EVENT_READ2
#define EVENT_MM_MISS_DIRTY EVENT_WRITE2
#define EVENT_NM_HIT        EVENT_READ
#define EVENT_M2M_CLOCKTICKS EVENT_WRITE

#define PCM_CPU_FAMILY_MODEL(family_, model) ((family_ << 8) | model)

// This list is ported from Intel's PCM implementation.
// For now, we only focus on Sapphire Rapids (SPR).
enum SupportedCPUModels {
    // NEHALEM_EP      = PCM_CPU_FAMILY_MODEL(6, 26),
    // NEHALEM         = PCM_CPU_FAMILY_MODEL(6, 30),
    // ATOM            = PCM_CPU_FAMILY_MODEL(6, 28),
    // ATOM_2          = PCM_CPU_FAMILY_MODEL(6, 53),
    // CENTERTON       = PCM_CPU_FAMILY_MODEL(6, 54),
    // BAYTRAIL        = PCM_CPU_FAMILY_MODEL(6, 55),
    // AVOTON          = PCM_CPU_FAMILY_MODEL(6, 77),
    // CHERRYTRAIL     = PCM_CPU_FAMILY_MODEL(6, 76),
    // APOLLO_LAKE     = PCM_CPU_FAMILY_MODEL(6, 92),
    // GEMINI_LAKE     = PCM_CPU_FAMILY_MODEL(6, 122),
    // DENVERTON       = PCM_CPU_FAMILY_MODEL(6, 95),
    // SNOWRIDGE       = PCM_CPU_FAMILY_MODEL(6, 134),
    // ELKHART_LAKE    = PCM_CPU_FAMILY_MODEL(6, 150),
    // JASPER_LAKE     = PCM_CPU_FAMILY_MODEL(6, 156),
    // CLARKDALE       = PCM_CPU_FAMILY_MODEL(6, 37),
    // WESTMERE_EP     = PCM_CPU_FAMILY_MODEL(6, 44),
    // NEHALEM_EX      = PCM_CPU_FAMILY_MODEL(6, 46),
    // WESTMERE_EX     = PCM_CPU_FAMILY_MODEL(6, 47),
    // SANDY_BRIDGE    = PCM_CPU_FAMILY_MODEL(6, 42),
    // JAKETOWN        = PCM_CPU_FAMILY_MODEL(6, 45),
    // IVY_BRIDGE      = PCM_CPU_FAMILY_MODEL(6, 58),
    // HASWELL         = PCM_CPU_FAMILY_MODEL(6, 60),
    // HASWELL_ULT     = PCM_CPU_FAMILY_MODEL(6, 69),
    // HASWELL_2       = PCM_CPU_FAMILY_MODEL(6, 70),
    // IVYTOWN         = PCM_CPU_FAMILY_MODEL(6, 62),
    // HASWELLX        = PCM_CPU_FAMILY_MODEL(6, 63),
    // BROADWELL       = PCM_CPU_FAMILY_MODEL(6, 61),
    // BROADWELL_XEON_E3 = PCM_CPU_FAMILY_MODEL(6, 71),
    // BDX_DE          = PCM_CPU_FAMILY_MODEL(6, 86),
    // SKL_UY          = PCM_CPU_FAMILY_MODEL(6, 78),
    // KBL             = PCM_CPU_FAMILY_MODEL(6, 158),
    // KBL_1           = PCM_CPU_FAMILY_MODEL(6, 142),
    // CML             = PCM_CPU_FAMILY_MODEL(6, 166),
    // CML_1           = PCM_CPU_FAMILY_MODEL(6, 165),
    // ICL             = PCM_CPU_FAMILY_MODEL(6, 126),
    // ICL_1           = PCM_CPU_FAMILY_MODEL(6, 125),
    // RKL             = PCM_CPU_FAMILY_MODEL(6, 167),
    // TGL             = PCM_CPU_FAMILY_MODEL(6, 140),
    // TGL_1           = PCM_CPU_FAMILY_MODEL(6, 141),
    // ADL             = PCM_CPU_FAMILY_MODEL(6, 151),
    // ADL_1           = PCM_CPU_FAMILY_MODEL(6, 154),
    // RPL             = PCM_CPU_FAMILY_MODEL(6, 0xb7),
    // RPL_1           = PCM_CPU_FAMILY_MODEL(6, 0xba),
    // RPL_2           = PCM_CPU_FAMILY_MODEL(6, 0xbf),
    // RPL_3           = PCM_CPU_FAMILY_MODEL(6, 0xbe),
    // MTL             = PCM_CPU_FAMILY_MODEL(6, 0xAA),
    // LNL             = PCM_CPU_FAMILY_MODEL(6, 0xBD),
    // ARL             = PCM_CPU_FAMILY_MODEL(6, 197),
    // ARL_1           = PCM_CPU_FAMILY_MODEL(6, 198),
    // BDX             = PCM_CPU_FAMILY_MODEL(6, 79),
    // KNL             = PCM_CPU_FAMILY_MODEL(6, 87),
    // SKL             = PCM_CPU_FAMILY_MODEL(6, 94),
    // SKX             = PCM_CPU_FAMILY_MODEL(6, 85),
    // ICX_D           = PCM_CPU_FAMILY_MODEL(6, 108),
    // ICX             = PCM_CPU_FAMILY_MODEL(6, 106),
    SPR             = PCM_CPU_FAMILY_MODEL(6, 143),
    // EMR             = PCM_CPU_FAMILY_MODEL(6, 207),
    // GNR             = PCM_CPU_FAMILY_MODEL(6, 173),
    // SRF             = PCM_CPU_FAMILY_MODEL(6, 175),
    // GNR_D           = PCM_CPU_FAMILY_MODEL(6, 174),
    // GRR             = PCM_CPU_FAMILY_MODEL(6, 182),
    END_OF_MODEL_LIST = 0x0ffff
};

struct hw_reg;

typedef struct {
    void (*write)(struct hw_reg* self, const uint64_t val);
    u64 (*read)(const struct hw_reg* self);
    void (*destroy)(struct hw_reg* self);
} hw_reg_ops;

typedef struct hw_reg {
    char* magic; // Magic number for type checking
    const hw_reg_ops* ops;
} hw_reg_t;

typedef struct mmio_reg32 {
    hw_reg_t hw_reg;
    mmio_range_t* handle;
    size_t offset;
} mmio_reg32_t;

static __always_inline u64
mmio_reg32_read(const hw_reg_t* self) {
    const mmio_reg32_t* reg = (const mmio_reg32_t*)self;
    return mmio_read32(reg->handle, reg->offset);
}

static __always_inline void
mmio_reg32_write(hw_reg_t* self, const u64 val) {
    const mmio_reg32_t* reg = (const mmio_reg32_t*)self;
    mmio_write32(reg->handle, reg->offset, (u32)val);
}

hw_reg_t* mmio_reg32_create(mmio_range_t* const handle, const size_t offset);
void mmio_reg32_destroy(hw_reg_t* self);

static const hw_reg_ops vt_mmio_reg32_ops = {
    .write = mmio_reg32_write,
    .read = mmio_reg32_read,
    .destroy = mmio_reg32_destroy,
};

typedef struct mmio_reg64 {
    hw_reg_t hw_reg;
    mmio_range_t* handle;
    u64 offset;
} mmio_reg64_t;

static __always_inline u64
mmio_reg64_read(const hw_reg_t* self) {
    const mmio_reg64_t* reg = (const mmio_reg64_t*)self;
    return mmio_read64(reg->handle, reg->offset);
}

static __always_inline void
mmio_reg64_write(hw_reg_t* self, const u64 val) {
    const mmio_reg64_t* reg = (const mmio_reg64_t*)self;
    mmio_write64(reg->handle, reg->offset, val);
}

hw_reg_t* mmio_reg64_create(mmio_range_t* const handle, const u64 offset);
void mmio_reg64_destroy(hw_reg_t* self);

static const hw_reg_ops vt_mmio_reg64_ops = {
    .write = mmio_reg64_write,
    .read = mmio_reg64_read,
    .destroy = mmio_reg64_destroy,
};

static __always_inline void
hw_reg_write(hw_reg_t* self, const u64 val) {
    if (self && self->ops && self->ops->write) {
        self->ops->write(self, val);
    } else {
        pr_err("kimc: hw_reg_write called with NULL or invalid ops\n");
    }
}

static __always_inline u64
hw_reg_read(const hw_reg_t* self) {
    if (self && self->ops && self->ops->read) {
        return self->ops->read(self);
    } else {
        pr_err("kimc: hw_reg_read called with NULL or invalid ops\n");
        return 0;
    }
}

static __always_inline bool
hw_reg_is_valid(const hw_reg_t* self) {
    return self && self->magic && strcmp(self->magic, HW_REG_MAGIC) == 0;
}

void hw_reg_destroy(hw_reg_t* self);

#endif // CPUICOUNTERS_H