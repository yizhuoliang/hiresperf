#ifndef _HRP_CONFIG_H
#define _HRP_CONFIG_H

/*
    Configuration macros
*/

#define HRP_BUFFER_SIZE 20
#define HRP_POLL_INTERVAL_US_LOW 200000
#define HRP_POLL_INTERVAL_US_HIGH 200100
#define HRP_POLLING_LOGGING_RATIO 10
#define HRP_LOG_PATH "/hrperf_log.bin"
#define HRP_LOGGER_CPU 0
#define HRP_CPU_SELECTION_MASK 0b1100UL

#define HRP_MAJOR_NUMBER 280
#define HRP_DEVICE_NAME "hrperf_device"
#define HRP_CLASS_NAME "hrperf_class"
#define HRP_IOC_MAGIC  'k'
#define HRP_IOC_START  _IO(HRP_IOC_MAGIC, 1)
#define HRP_IOC_PAUSE   _IO(HRP_IOC_MAGIC, 2)

#endif