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

#include "buffer.h"
#include "config.h"
#include "intel-msr.h"
#include "pmc.h"
#include "log.h"

// for the logger and pollers
static DEFINE_PER_CPU(HrperfRingBuffer, per_cpu_buffer);
static DEFINE_PER_CPU(struct task_struct *, per_cpu_thread);
static struct task_struct *logger_thread;
struct file *log_file;

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

static bool hrperf_running = false;

static inline __attribute__((always_inline)) uint64_t read_tsc(void)
{
  uint32_t a, d;
  asm volatile("rdtsc" : "=a" (a), "=d" (d));
  return ((uint64_t)a) | (((uint64_t)d) << 32);
}

// Per-cpu thread function for polling the PMCs
static int hrperf_per_cpu_poller(void *arg) {
    // enable the counters
    wrmsrl(MSR_IA32_FIXED_CTR_CTRL, 0x030); // fixed counter 1 for cpu unhalt
    wrmsrl(MSR_IA32_GLOBAL_CTRL, 1UL | (1UL << 1)  | (1UL << 33)); // arch 0,1, fixed 1

    // make event selections
    wrmsrl(MSR_IA32_PERFEVTSEL0, PMC_LLC_MISSES_FINAL);
    wrmsrl(MSR_IA32_PERFEVTSEL1, PMC_SW_PREFETCH_ANY_SKYLAKE_FINAL);

    HrperfTick tick;

    // start polling
    while (!kthread_should_stop()) {
        tick.tsc = read_tsc();
        rdmsrl(MSR_IA32_FIXED_CTR1, tick.cpu_unhalt);
        rdmsrl(MSR_IA32_PMC0, tick.llc_misses);
        rdmsrl(MSR_IA32_PMC1, tick.sw_prefetch);

        enqueue(this_cpu_ptr(&per_cpu_buffer), tick);
        usleep_range(HRP_POLL_INTERVAL_US, HRP_POLL_INTERVAL_US + 100);
    }

    // reset the control MSRs to zeros
    wrmsrl(MSR_IA32_FIXED_CTR_CTRL, 0UL);
    wrmsrl(MSR_IA32_GLOBAL_CTRL, 0UL);
    return 0;
}

// Logger/printer thread function
static int hrperf_logger(void *arg) {
    while (!kthread_should_stop()) {
        if (!hrperf_running) {
            set_current_state(TASK_INTERRUPTIBLE);
            schedule();  // pause execution here
        }

        if (kthread_should_stop()) break;

        usleep_range(HRP_POLL_INTERVAL_US * 10, HRP_POLL_INTERVAL_US * 11);
        int cpu;
        for_each_possible_cpu(cpu) {
            if (HRP_CPU_SELECTION_MASK & (1UL << cpu)) {
                printk(KERN_INFO "CPU %d: ", cpu);
                log_and_clear(per_cpu_ptr(&per_cpu_buffer, cpu), cpu, log_file);
            }
        }
    }
    return 0;
}

// IOCTL function for start/stop the logger/pollers
static long hrperf_ioctl(struct file *file, unsigned int cmd, unsigned long arg) {
    int cpu;

    switch (cmd) {
    case HRP_IOC_START:
        if (!hrperf_running) {
            hrperf_running = true;
            // Start all paused threads
            for_each_possible_cpu(cpu) {
                if (HRP_CPU_SELECTION_MASK & (1UL << cpu)) {
                    struct task_struct *t = per_cpu(per_cpu_thread, cpu);
                    if (t) {
                        wake_up_process(t);
                    } else {
                        printk(KERN_ALERT "hrperf: guess what? a puller thread corrupted\n");
                        return -1;
                    }
                }
            }
            if (logger_thread) {
                wake_up_process(logger_thread);
            } else {
                printk(KERN_ALERT "hrperf: guess what? the logger thread corrupted\n");
                return -1;
            }
            printk(KERN_INFO "hrperf: Monitoring resumed\n");
        }
        break;
    case HRP_IOC_PAUSE:
        if (hrperf_running) {
            hrperf_running = false;
            // Signal threads to pause
            for_each_possible_cpu(cpu) {
                if (HRP_CPU_SELECTION_MASK & (1UL << cpu)) {
                    struct task_struct *t = per_cpu(per_cpu_thread, cpu);
                    if (t) {
                        send_sig(SIGSTOP, t, 0);
                    }
                }
            }
            if (logger_thread) {
                send_sig(SIGSTOP, logger_thread, 0);
            }
            printk(KERN_INFO "hrperf: Monitoring paused\n");
        }
        break;
    default:
        return -ENOTTY;
    }
    return 0;
}

static int __init hrperf_init(void) {
    printk(KERN_INFO "hrperf: Initializing LKM\n");

    // step 1: init char device
    dev_t dev_num = MKDEV(HRP_MAJOR_NUMBER, 0);
    if (register_chrdev_region(dev_num, 1, HRP_DEVICE_NAME) < 0) {
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

    dev_class = class_create(THIS_MODULE, HRP_CLASS_NAME);
    if (IS_ERR(dev_class)) {
        unregister_chrdev_region(dev_num, 1);
        cdev_del(&char_dev);
        printk(KERN_ALERT "hrperf: failed to register device class\n");
        return PTR_ERR(dev_class);
    }
    printk(KERN_INFO "hrperf: device class registered\n");

    device_p = device_create(dev_class, NULL, dev_num, NULL, HRP_DEVICE_NAME);
    if (IS_ERR(device_p)) {
        class_destroy(dev_class);
        cdev_del(&char_dev);
        unregister_chrdev_region(dev_num, 1);
        printk(KERN_ALERT "hrperf: failed to create the device\n");
        return PTR_ERR(device_p);
    }
    printk(KERN_INFO "hrperf: device setup done\n");

    // step 2: init poller threads
    int cpu;
    for_each_possible_cpu(cpu) {
        if (HRP_CPU_SELECTION_MASK & (1UL << cpu)) {
            struct task_struct *thread;
            init_ring_buffer(per_cpu_ptr(&per_cpu_buffer, cpu));
            thread = kthread_create_on_node(hrperf_per_cpu_poller, NULL, cpu_to_node(cpu), "per_cpu_poller_thread_%d", cpu);
            kthread_bind(thread, cpu);
            wake_up_process(thread);
            per_cpu(per_cpu_thread, cpu) = thread;
        }
    }

    // step 3: init log file
    log_file = hrperf_init_log_file();
    if (log_file == NULL) {
        printk(KERN_ERR "Failed to initialize log file\n");
        // Handle the error appropriately
    }

    // step 4: init logger thread
    logger_thread = kthread_run(hrperf_logger, NULL, "logger_thread");
    kthread_bind(logger_thread, HRP_LOGGER_CPU);

    return 0;
}

static void __exit hrperf_exit(void) {

    int cpu;
    for_each_possible_cpu(cpu) {
        if (HRP_CPU_SELECTION_MASK & (1UL << cpu)) {
            struct task_struct *t = per_cpu(per_cpu_thread, cpu);
            if (t) {
                kthread_stop(t);
            }
        }
    }

    if (logger_thread) {
        kthread_stop(logger_thread);
    }

    hrperf_close_log_file(log_file);

    dev_t dev_num = MKDEV(major_number, 0);
    device_destroy(dev_class, dev_num);
    class_destroy(dev_class);
    cdev_del(&char_dev);
    unregister_chrdev_region(dev_num, 1);

    printk(KERN_INFO "hrperf: Cleaned up module\n");
}

module_init(hrperf_init);
module_exit(hrperf_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Coulson Liang");
MODULE_DESCRIPTION("High-resolution Performance Monitor");
