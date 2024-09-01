/*
 * pmc.h - useful definitions for Intel Performance Counters
 */

#pragma once

#define PMC_ESEL_UMASK_SHIFT    8
#define PMC_ESEL_CMASK_SHIFT    24
#define PMC_ESEL_ENTRY(event, umask, cmask)		\
        (((event) & 0xFFUL) |				\
         (((umask) & 0xFFUL) << PMC_ESEL_UMASK_SHIFT) |	\
         (((cmask) & 0xFFUL) << PMC_ESEL_CMASK_SHIFT))
#define PMC_ESEL_USR            (1ULL << 16) /* User Mode */
#define PMC_ESEL_OS             (1ULL << 17) /* Kernel Mode */
#define PMC_ESEL_EDGE           (1ULL << 18) /* Edge detect */
#define PMC_ESEL_PC             (1ULL << 19) /* Pin control */
#define PMC_ESEL_INT            (1ULL << 20) /* APIC interrupt enable */
#define PMC_ESEL_ANY            (1ULL << 21) /* Any thread */
#define PMC_ESEL_ENABLE         (1ULL << 22) /* Enable counters */
#define PMC_ESEL_INV            (1ULL << 23) /* Invert counter mask */

/* architectural performance counters (works on all Intel CPUs) */
#define PMC_ARCH_CORE_CYCLES    PMC_ESEL_ENTRY(0x3C, 0x00, 0)
#define PMC_ARCH_INSTR_RETIRED  PMC_ESEL_ENTRY(0xC0, 0x00, 0)
#define PMC_ARCH_REF_CYCLES     PMC_ESEL_ENTRY(0x3C, 0x01, 0)
#define PMC_ARCH_LLC_REF        PMC_ESEL_ENTRY(0x2E, 0x4F, 0)
#define PMC_ARCH_LLC_MISSES     PMC_ESEL_ENTRY(0x2E, 0x41, 0)
#define PMC_ARCH_BRANCHES       PMC_ESEL_ENTRY(0xC4, 0x00, 0)
#define PMC_ARCH_BRANCH_MISSES  PMC_ESEL_ENTRY(0xC5, 0x00, 0)

/* non-architectural pmcs has different selecto definitions for each microarchitecture */
#define PMC_SW_PREFETCH_ANY_SKYLAKE             PMC_ESEL_ENTRY(0x32, 0x0F, 0)
#define PMC_CYCLE_STALLS_L3_MISS_SKYLAKE        PMC_ESEL_ENTRY(0xA3, 0x06, 0x06) // https://perfmon-events.intel.com/index.html?pltfrm=skylake_server.html&evnt=CYCLE_ACTIVITY.STALLS_L3_MISS
#define PMC_CYCLE_STALLLS_MEM_SKYLAKE           PMC_ESEL_ENTRY(0xA3, 0x14, 0x14)
#define PMC_CYCLE_OUTSTANDING_MEM_SKYLAKE       PMC_ESEL_ENTRY(0xA3, 0x10, 0x10)

/* Final composed 64 bit to put into esel register */
#define PMC_LLC_MISSES_FINAL (PMC_ARCH_LLC_MISSES | PMC_ESEL_USR | PMC_ESEL_OS | \
			PMC_ESEL_ENABLE)

#define PMC_SW_PREFETCH_ANY_SKYLAKE_FINAL (PMC_SW_PREFETCH_ANY_SKYLAKE | PMC_ESEL_USR | PMC_ESEL_OS | \
			PMC_ESEL_ENABLE)

#define PMC_CYCLE_STALLS_L3_MISS_SKYLAKE_FINAL (PMC_CYCLE_STALLS_L3_MISS_SKYLAKE | PMC_ESEL_USR | PMC_ESEL_OS | \
                        PMC_ESEL_ENABLE)

#define PMC_CYCLE_STALLS_MEM_SKYLAKE_FINAL (PMC_CYCLE_STALLLS_MEM_SKYLAKE  | PMC_ESEL_USR | PMC_ESEL_OS | \
                        PMC_ESEL_ENABLE)

#define PMC_CYCLE_OUTSTANDING_MEM_SKYLAKE_FINAL (PMC_CYCLE_OUTSTANDING_MEM_SKYLAKE | PMC_ESEL_USR | PMC_ESEL_OS | \
                        PMC_ESEL_ENABLE)
