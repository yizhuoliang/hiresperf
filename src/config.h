#ifndef _HRP_CONFIG_H
#define _HRP_CONFIG_H

/*
    Configuration macros
*/

#define HRP_PMC_BUFFER_SIZE 50
#define HRP_PMC_POLL_INTERVAL_US_LOW 50
#define HRP_PMC_POLL_INTERVAL_US_HIGH 65
#define HRP_PMC_POLLING_LOGGING_RATIO 35
#define HRP_PMC_LOG_PATH "/hrperf_log.bin"
#define HRP_PMC_LOGGER_CPU 0
#define HRP_PMC_CPU_SELECTION_MASK 0b1100UL

#define HRP_PMC_MAJOR_NUMBER 280
#define HRP_PMC_DEVICE_NAME "hrperf_device"
#define HRP_PMC_CLASS_NAME "hrperf_class"
#define HRP_PMC_IOC_MAGIC  'k'
#define HRP_PMC_IOC_START  _IO(HRP_PMC_IOC_MAGIC, 1)
#define HRP_PMC_IOC_PAUSE   _IO(HRP_PMC_IOC_MAGIC, 2)

#endif