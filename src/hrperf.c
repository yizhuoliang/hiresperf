#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/percpu.h>
#include <linux/kthread.h>
#include <linux/sched.h>
#include <linux/delay.h>
#include <linux/ioctl.h>
#include <linux/sched/signal.h>
#include <linux/cdev.h>
#include <linux/device.h>
#include <linux/fs.h>
#include <linux/ktime.h>
#include <linux/smp.h>

#include "buffer.h"
#include "config.h"
#include "intel_msr.h"
#include "intel_pmc.h"
#include "log.h"

// for the poller, logger, and buffers
static DEFINE_PER_CPU(HrperfRingBuffer, per_cpu_buffer);
static struct task_struct *poller_thread;
static struct task_struct *logger_thread;
struct file *log_file;
struct cpumask hrp_selected_cpus;
static bool hrperf_running = false;

// for the char device
static int major_number;
static struct class* dev_class = NULL;
static struct device* device_p = NULL;
static struct cdev char_dev;

static long hrperf_ioctl(struct file *file, unsigned int cmd, unsigned long arg);
static struct file_operations fops = {
    .owner = THIS_MODULE,
    .unlocked_ioctl = hrperf_ioctl
};

static inline __attribute__((always_inline)) uint64_t read_tsc(void)
{
  uint32_t a, d;
  asm volatile("rdtsc" : "=a" (a), "=d" (d));
  return ((uint64_t)a) | (((uint64_t)d) << 32);
}

static void hrperf_pmc_enable_and_esel(void *info) {
    // enable the counters
    wrmsrl(MSR_IA32_FIXED_CTR_CTRL, 0x030); // fixed counter 1 for cpu unhalt
    wrmsrl(MSR_IA32_GLOBAL_CTRL, 1UL | (1UL << 1) | (1UL << 2) | (1UL << 3) | (1UL << 33)); // arch 0,1,2,3, fixed 1

    // make event selections
    wrmsrl(MSR_IA32_PERFEVTSEL0, PMC_LLC_MISSES_FINAL);
    wrmsrl(MSR_IA32_PERFEVTSEL1, PMC_SW_PREFETCH_ANY_SKYLAKE_FINAL);
    wrmsrl(MSR_IA32_PERFEVTSEL2, PMC_CYCLE_STALLS_MEM_SKYLAKE_FINAL);
    wrmsrl(MSR_IA32_PERFEVTSEL3, PMC_CYCLE_OUTSTANDING_MEM_SKYLAKE_FINAL);
}

// Function to be called on each CPU by smp_call_function_many
static void hrperf_poller_func(void *info) {
    HrperfTick tick;
    tick.kts = ktime_get_raw();
    rdmsrl(MSR_IA32_PMC2, tick.stall_mem);
    rdmsrl(MSR_IA32_PMC3, tick.cycle_mem);
    rdmsrl(MSR_IA32_FIXED_CTR1, tick.cpu_unhalt);
    rdmsrl(MSR_IA32_PMC0, tick.llc_misses);
    rdmsrl(MSR_IA32_PMC1, tick.sw_prefetch);
    enqueue(this_cpu_ptr(&per_cpu_buffer), tick);
}

// Single poller thread function for initiating the smp_call_function_many
static int hrperf_poller_thread(void *arg) {
    while (!kthread_should_stop()) {
        if (!hrperf_running) {
            set_current_state(TASK_INTERRUPTIBLE);
            schedule();  // pause execution here
        }

        smp_call_function_many(&hrp_selected_cpus, hrperf_poller_func, NULL, 1);
        usleep_range(HRP_PMC_POLL_INTERVAL_US_LOW, HRP_PMC_POLL_INTERVAL_US_HIGH);
    }
    return 0;
}

// Logger thread function
static int hrperf_logger_thread(void *arg) {
    while (!kthread_should_stop()) {
        if (!hrperf_running) {
            set_current_state(TASK_INTERRUPTIBLE);
            schedule();  // pause execution here
        }

        if (kthread_should_stop()) break;

        usleep_range(HRP_PMC_POLL_INTERVAL_US_LOW * HRP_PMC_POLLING_LOGGING_RATIO, HRP_PMC_POLL_INTERVAL_US_HIGH * HRP_PMC_POLLING_LOGGING_RATIO);
        int cpu;
        for_each_possible_cpu(cpu) {
            if (HRP_PMC_CPU_SELECTION_MASK & (1UL << cpu)) {
                log_and_clear(per_cpu_ptr(&per_cpu_buffer, cpu), cpu, log_file);
            }
        }
    }
    return 0;
}

// IOCTL function to start/stop the logger/pollers
static long hrperf_ioctl(struct file *file, unsigned int cmd, unsigned long arg) {
    switch (cmd) {
    case HRP_PMC_IOC_START:
        if (!hrperf_running) {
            hrperf_running = true;
            wake_up_process(poller_thread);
            wake_up_process(logger_thread);
            printk(KERN_INFO "hrperf: Monitoring resumed\n");
        }
        break;
    case HRP_PMC_IOC_PAUSE:
        if (hrperf_running) {
            hrperf_running = false;
            printk(KERN_INFO "hrperf: Monitoring paused\n");
        }
        break;
    default:
        return -ENOTTY;
    }
    return 0;
}

static int __init hrp_pmc_init(void) {
    printk(KERN_INFO "hrperf: Initializing LKM\n");

    // step 1: init char device
    dev_t dev_num = MKDEV(HRP_PMC_MAJOR_NUMBER, 0);
    if (register_chrdev_region(dev_num, 1, HRP_PMC_DEVICE_NAME) < 0) {
        printk(KERN_ALERT "hrperf: failed to register a major number\n");
        return -1;
    }
    major_number = MAJOR(dev_num);
    printk(KERN_INFO "hrperf: registered with major number %d\n", major_number);

    cdev_init(&char_dev, &fops);
    char_dev.owner = THIS_MODULE;
    if (cdev_add(&char_dev, dev_num, 1) < 0) {
        unregister_chrdev_region(dev_num, 1);
        printk(KERN_ALERT "hrperf: failed to add cdev\n");
        return -1;
    }

    dev_class = class_create(THIS_MODULE, HRP_PMC_CLASS_NAME);
    if (IS_ERR(dev_class)) {
        unregister_chrdev_region(dev_num, 1);
        cdev_del(&char_dev);
        printk(KERN_ALERT "hrperf: failed to register device class\n");
        return PTR_ERR(dev_class);
    }
    printk(KERN_INFO "hrperf: device class registered\n");

    device_p = device_create(dev_class, NULL, dev_num, NULL, HRP_PMC_DEVICE_NAME);
    if (IS_ERR(device_p)) {
        class_destroy(dev_class);
        cdev_del(&char_dev);
        unregister_chrdev_region(dev_num, 1);
        printk(KERN_ALERT "hrperf: failed to create the device\n");
        return PTR_ERR(device_p);
    }
    printk(KERN_INFO "hrperf: device setup done\n");

    // step 2.1: init selected cpus
    cpumask_clear(&hrp_selected_cpus);
    int cpu;
    for_each_possible_cpu(cpu) {
        if (HRP_PMC_CPU_SELECTION_MASK & (1UL << cpu)) {
            cpumask_set_cpu(cpu, &hrp_selected_cpus);
        }
    }

    // step 2.2: enable the counters and make event selections
    smp_call_function_many(&hrp_selected_cpus, hrperf_pmc_enable_and_esel, NULL, 1);

    // Initialize poller thread
    poller_thread = kthread_run(hrperf_poller_thread, NULL, "poller_thread");
    if (IS_ERR(poller_thread)) {
        printk(KERN_ERR "Failed to create the poller thread\n");
        return PTR_ERR(poller_thread);
    }

    // step 3: init log file
    log_file = hrperf_init_log_file();
    if (log_file == NULL) {
        printk(KERN_ERR "Failed to initialize log file\n");
        // Handle the error appropriately
    }
    
    // Initialize logger thread
    logger_thread = kthread_run(hrperf_logger_thread, NULL, "logger_thread");
    if (IS_ERR(logger_thread)) {
        printk(KERN_ERR "Failed to create the logger thread\n");
        return PTR_ERR(logger_thread);
    }
    kthread_bind(logger_thread, HRP_PMC_LOGGER_CPU);

    return 0;
}

static void __exit hrp_pmc_exit(void) {
    if (logger_thread) {
        kthread_stop(logger_thread);
    }

    if (poller_thread) {
        kthread_stop(poller_thread);
    }

    hrperf_close_log_file(log_file);

    dev_t dev_num = MKDEV(major_number, 0);
    device_destroy(dev_class, dev_num);
    class_destroy(dev_class);
    cdev_del(&char_dev);
    unregister_chrdev_region(dev_num, 1);

    printk(KERN_INFO "hrperf: Cleaned up module\n");
}

module_init(hrp_pmc_init);
module_exit(hrp_pmc_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Coulson Liang");
MODULE_DESCRIPTION("High-resolution Performance Monitor");