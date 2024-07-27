#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/percpu.h>
#include <linux/kthread.h>
#include <linux/sched.h>
#include "ring_buffer.c"

#define POLL_INTERVAL 1000000  // 1000000 microseconds

static DEFINE_PER_CPU(RingBuffer, cpu_buffers);
static DEFINE_PER_CPU(struct task_struct *, per_cpu_thread);
static struct task_struct *printer_thread;

// Function for each per-CPU thread
static int per_cpu_func(void *data) {
    while (!kthread_should_stop()) {
        u64 timestamp;
        asm volatile("rdtsc" : "=A"(timestamp));
        enqueue(this_cpu_ptr(&cpu_buffers), timestamp);
        usleep_range(POLL_INTERVAL, POLL_INTERVAL + 100);
    }
    return 0;
}

// Printer thread function
static int printer_func(void *data) {
    while (!kthread_should_stop()) {
        int cpu;
        for_each_possible_cpu(cpu) {
            printk(KERN_INFO "CPU %d: ", cpu);
            print_and_clear(per_cpu_ptr(&cpu_buffers, cpu));
        }
        usleep_range(POLL_INTERVAL * 10, POLL_INTERVAL * 10 + 1000);  // Sleep 10 times the poll interval
    }
    return 0;
}

static int __init per_core_task_init(void) {
    int cpu;

    for_each_possible_cpu(cpu) {
        struct task_struct *thread;
        init_ring_buffer(per_cpu_ptr(&cpu_buffers, cpu));
        thread = kthread_create_on_node(per_cpu_func, NULL, cpu_to_node(cpu), "per_cpu_thread_%d", cpu);
        kthread_bind(thread, cpu);
        wake_up_process(thread);
        per_cpu(per_cpu_thread, cpu) = thread;
    }

    printer_thread = kthread_run(printer_func, NULL, "printer_thread");
    return 0;
}

static void __exit per_core_task_exit(void) {
    int cpu;
    for_each_possible_cpu(cpu) {
        kthread_stop(per_cpu(per_cpu_thread, cpu));
    }
    kthread_stop(printer_thread);
}

module_init(per_core_task_init);
module_exit(per_core_task_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Your Name");
MODULE_DESCRIPTION("Per-CPU Task with Ring Buffer");
