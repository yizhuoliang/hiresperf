#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/percpu.h>
#include <linux/kthread.h>
#include <linux/sched.h>
#include <linux/delay.h>

#include "buffer.h"
#include "config.h"
#include "intel-msr.h"
#include "pmc.h"
#include "log.h"

static DEFINE_PER_CPU(HrperfRingBuffer, per_cpu_buffer);
static DEFINE_PER_CPU(struct task_struct *, per_cpu_thread);
static struct task_struct *logger_thread;
struct file *log_file;

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
        asm volatile("rdtsc" : "=A"(tick.tsc));
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
        int cpu;
        for_each_possible_cpu(cpu) {
            printk(KERN_INFO "CPU %d: ", cpu);
            log_and_clear(per_cpu_ptr(&per_cpu_buffer, cpu), cpu, log_file);
        }
        usleep_range(HRP_POLL_INTERVAL_US * 10, HRP_POLL_INTERVAL_US * 11);
    }
    return 0;
}

static int __init hrperf_init(void) {
    // init poller thread
    int cpu;
    for_each_possible_cpu(cpu) {
        struct task_struct *thread;
        init_ring_buffer(per_cpu_ptr(&per_cpu_buffer, cpu));
        thread = kthread_create_on_node(hrperf_per_cpu_poller, NULL, cpu_to_node(cpu), "per_cpu_poller_thread_%d", cpu);
        kthread_bind(thread, cpu);
        wake_up_process(thread);
        per_cpu(per_cpu_thread, cpu) = thread;
    }

    // init log file
    log_file = hrperf_init_log_file();
    if (log_file == NULL) {
        printk(KERN_ERR "Failed to initialize log file\n");

    }

    // init logger thread
    logger_thread = kthread_run(hrperf_logger, NULL, "logger_thread");

    return 0;
}

static void __exit hrperf_exit(void) {
    // stop the threads
    int cpu;
    for_each_possible_cpu(cpu) {
        kthread_stop(per_cpu(per_cpu_thread, cpu));
    }
    kthread_stop(logger_thread);
    hrperf_close_log_file(log_file);
}

module_init(hrperf_init);
module_exit(hrperf_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Coulson Liang");
MODULE_DESCRIPTION("High-resolution Performance Monitor");