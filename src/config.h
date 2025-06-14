#ifndef _HRP_CONFIG_H
#define _HRP_CONFIG_H

/*
    PMC Polling Component Configurations
*/

// the size for each core's PMC evnets buffer for poller/logger
#define HRP_PMC_BUFFER_SIZE 50

// the poller thread will sleep for this interval, in microseconds
#define HRP_PMC_POLL_INTERVAL_US_LOW 10
#define HRP_PMC_POLL_INTERVAL_US_HIGH 12

// how many rounds of PMC polling before each logging
#define HRP_PMC_POLLING_LOGGING_RATIO 35

#define HRP_PMC_LOG_PATH "/hrperf_log.bin"

#define HRP_PMC_LOGGER_CPU 0
#define HRP_PMC_POLLER_CPU 1

// set to 1 for ktime_get_raw(), 0 for ktime_get_real()
// LDB timestamps use the raw clock, also corresponding to perf sched record -k CLOCK_MONOTONIC_RAW
#define HRP_USE_RAW_CLOCK 0

// 1 for using rdtsc for timestamping, 0 for using ktime_get
// if set to 1, the HRP_USE_RAW_CLOCK flag will be ignored.
#define HRP_USE_TSC 0

// set to 1 to enable user space polling via RDPMC
#define ENABLE_USER_SPACE_POLLING 1

// the bitmask for selecting which cores to monitor
#define HRP_PMC_CPU_SELECTION_MASK_BITS 256
static const unsigned long hrp_pmc_cpu_selection_mask_bits[HRP_PMC_CPU_SELECTION_MASK_BITS / 64] = {
    0b0000000000000000111111111111111111111111111111111111111111111111UL, // CPUs 63-0
    0b0000000000000000000000000000000000000000000000000000000000000000UL, // CPUs 127-64
    0b0000000000000000000000000000000000000000000000000000000000000000UL, // CPUs 191-128
    0b0000000000000000000000000000000000000000000000000000000000000000UL  // CPUs 255-192
};

/*
    Hardware PMU Configurations
*/
// define the architecture name for configuring offcore PMUs
// Currently supported cache-miss/prefetch: "SKYLAKE", "ICELAKE", "SAPPHIRE"
// Currently supported offcore: "SAPPHIRE"
// to add a new arch, add macros in intel_pmc.h and update this macro
#define HRP_ARCH_NAME SAPPHIRE

#define HRP_USE_OFFCORE 1 // set to 1 for using offcore reads/writes PMUs, 0 for using cache-miss/prefetch PMUs


// DO NOT EDIT! These will be composed with the above HRP_ARCH_NAME
// Helper macros for proper token pasting
#define PMC_PASTE_HELPER(a, b, c) a##b##c
#define PMC_PASTE(a, b, c) PMC_PASTE_HELPER(a, b, c)

// Offcore-specific macros
#define PMC_OCR_READS_TO_CORE_DRAM_ARCH_FINAL          PMC_PASTE(PMC_OCR_READS_TO_CORE_DRAM_, HRP_ARCH_NAME, _FINAL)
#define PMC_OCR_READS_TO_CORE_DRAM_RSP_ARCH            PMC_PASTE(PMC_OCR_READS_TO_CORE_DRAM_RSP_, HRP_ARCH_NAME, )
#define PMC_OCR_MODIFIED_WRITE_ANY_RESPONSE_ARCH_FINAL PMC_PASTE(PMC_OCR_MODIFIED_WRITE_ANY_RESPONSE_, HRP_ARCH_NAME, _FINAL)
#define PMC_OCR_MODIFIED_WRITE_ANY_RESPONSE_RSP_ARCH   PMC_PASTE(PMC_OCR_MODIFIED_WRITE_ANY_RESPONSE_RSP_, HRP_ARCH_NAME, )
// Cache-miss and prefetch macros
#define PMC_SW_PREFETCH_ANY_ARCH_FINAL                 PMC_PASTE(PMC_SW_PREFETCH_ANY_, HRP_ARCH_NAME, _FINAL)
#define PMC_CYCLE_STALLS_MEM_ARCH_FINAL                PMC_PASTE(PMC_CYCLE_STALLS_MEM_, HRP_ARCH_NAME, _FINAL)

/*
    Device Configurations
*/

#define HRP_PMC_MAJOR_NUMBER 283
#define HRP_PMC_DEVICE_NAME "hrperf_device"
#define HRP_PMC_CLASS_NAME "hrperf_class"
#define HRP_PMC_IOC_MAGIC  'k'
#define HRP_PMC_IOC_START       _IO(HRP_PMC_IOC_MAGIC, 1)
#define HRP_PMC_IOC_PAUSE       _IO(HRP_PMC_IOC_MAGIC, 2)
#define HRP_PMC_IOC_TSC_FREQ    _IOR(HRP_PMC_IOC_MAGIC, 10, u64)

/*
    BPF Component Configurations, CURRENTLY NOT USED!
*/

// This polling interval doesn't affect the timeline accuracy,
// since each event has timestamps of start and return times
#define HRP_BPF_BUFFER_SIZE (1 << 15) 
#define HRP_BPF_POLL_INTERVAL_US 1000

#define HRP_BPF_ENABLE_TCP 0
#define HRP_BPF_ENABLE_UDP 1
#define HRP_BPF_ENABLE_BLK 1

#define HRP_BPF_POLLING_CPU 3

// the ssh process by default send tcp every 100us, pollutes the log
// so a non-zero value here will exclude the process from being logged
#define HRP_BPF_SSH_PID 0

#define HRP_BPF_LOG_FILE_PATH "/bpf_log.bin"
#define HRP_BPF_LOG_FILE_SIZE (1024 * 1024 * 200)

#endif
