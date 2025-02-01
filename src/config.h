#ifndef _HRP_CONFIG_H
#define _HRP_CONFIG_H

/*
    PMC Component Configurations
*/

#define HRP_PMC_BUFFER_SIZE 50
#define HRP_PMC_POLL_INTERVAL_US_LOW 1000
#define HRP_PMC_POLL_INTERVAL_US_HIGH 1050
#define HRP_PMC_POLLING_LOGGING_RATIO 35
#define HRP_PMC_LOG_PATH "/hrperf_log.bin"
#define HRP_PMC_LOGGER_CPU 2
#define HRP_PMC_POLLER_CPU 3

#define HRP_PMC_CPU_SELECTION_MASK_BITS 256
static const unsigned long hrp_pmc_cpu_selection_mask_bits[HRP_PMC_CPU_SELECTION_MASK_BITS / BITS_PER_LONG] = {
    0b0000000011111111111111111111111111111111111111111111111111111111UL, // CPUs 63-0
    0b0000000000000000000000000000000000000000000000000000000000000000UL, // CPUs 127-64
    0b0000000000000000000000000000000000000000000000000000000000000000UL, // CPUs 191-128
    0b0000000000000000000000000000000000000000000000000000000000000000UL  // CPUs 255-192
};

#define HRP_PMC_MAJOR_NUMBER 280
#define HRP_PMC_DEVICE_NAME "hrperf_device"
#define HRP_PMC_CLASS_NAME "hrperf_class"
#define HRP_PMC_IOC_MAGIC  'k'
#define HRP_PMC_IOC_START  _IO(HRP_PMC_IOC_MAGIC, 1)
#define HRP_PMC_IOC_PAUSE   _IO(HRP_PMC_IOC_MAGIC, 2)

/*
    BPF Component Configurations
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
