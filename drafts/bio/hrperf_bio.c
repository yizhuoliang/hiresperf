#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/bio.h>
#include <linux/blkdev.h>
#include <linux/kprobes.h>
#include <linux/sched.h>
#include <linux/slab.h>

#define LOG_BUF_SIZE (100 * 1024 * 1024) // 100MB

static char *log_buf;
static size_t log_pos = 0;

// Custom function to append data to the log buffer
static void log_to_buffer(const char *fmt, ...) {
    va_list args;
    int written;

    if (log_pos < LOG_BUF_SIZE) {
        va_start(args, fmt);
        written = vsnprintf(log_buf + log_pos, LOG_BUF_SIZE - log_pos, fmt, args);
        va_end(args);

        if (written > 0) {
            log_pos += written;
        }
    }
}

static int handler_pre(struct kprobe *p, struct pt_regs *regs) {
    struct bio *bio = (struct bio *)regs->di;

    log_to_buffer("submit_bio: bio=%p, sector=%llu, op=%x, size=%u, start_time=%llu, pid=%d, tgid=%d\n",
                  bio, (unsigned long long)bio->bi_iter.bi_sector,
                  bio_op(bio), bio->bi_iter.bi_size, ktime_get_ns(),
                  current->pid, current->tgid);

    return 0;
}

static int handler_ret(struct kretprobe_instance *ri, struct pt_regs *regs) {
    int ret_val = (int)regs->ax;
    log_to_buffer("submit_bio returned with %d\n", ret_val);
    return 0;
}

static struct kprobe kp = {
    .symbol_name    = "submit_bio",
    .pre_handler    = handler_pre,
};

static struct kretprobe kr = {
    .kp = {
        .symbol_name = "submit_bio",
    },
    .handler = handler_ret,
};

static int __init kprobe_init(void) {
    int ret;

    log_buf = vzalloc(LOG_BUF_SIZE);
    if (!log_buf)
        return -ENOMEM;

    ret = register_kprobe(&kp);
    if (ret < 0) {
        vfree(log_buf);
        log_buf = NULL;
        printk(KERN_INFO "Failed to register kprobe: %d\n", ret);
        return ret;
    }

    ret = register_kretprobe(&kr);
    if (ret < 0) {
        unregister_kprobe(&kp);
        vfree(log_buf);
        log_buf = NULL;
        printk(KERN_INFO "Failed to register kretprobe: %d\n", ret);
        return ret;
    }

    return 0;
}

static void __exit kprobe_exit(void) {
    unregister_kprobe(&kp);
    unregister_kretprobe(&kr);
    if (log_buf) {
        printk(KERN_INFO "Dumping log buffer...\n");
        // Note: Directly dumping large data with printk can lead to system instability
        printk(KERN_CONT "%s", log_buf);
        vfree(log_buf);
        log_buf = NULL;
    }
}

module_init(kprobe_init);
module_exit(kprobe_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Your Name");
MODULE_DESCRIPTION("A module to monitor block IO with buffered entry and exit tracing");
