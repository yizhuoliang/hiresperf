#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/bio.h>
#include <linux/blkdev.h>
#include <linux/kprobes.h>
#include <linux/sched.h>

static int handler_pre(struct kprobe *p, struct pt_regs *regs) {
    struct bio *bio = (struct bio *)regs->di; // For x86_64, the first argument is in DI register

    printk(KERN_INFO "submit_bio: bio=%p, sector=%llu, op=%x, size=%u, start_time=%llu, pid=%d, tgid=%d\n",
           bio, (unsigned long long)bio->bi_iter.bi_sector,
           bio_op(bio), bio->bi_iter.bi_size, ktime_get_ns(),
           current->pid, current->tgid);  // Add process ID and thread group ID

    return 0;
}

static int handler_ret(struct kretprobe_instance *ri, struct pt_regs *regs) {
    int ret_val = regs_return_value(regs);
    printk(KERN_INFO "submit_bio returned with %d\n", ret_val);
    return 0;
}

static struct kprobe kp = {
    .symbol_name    = "submit_bio",
    .pre_handler    = handler_pre,
    // Optionally you could also define a post_handler if needed
};

static struct kretprobe kr = {
    .kp = {
        .symbol_name = "submit_bio",
    },
    .handler = handler_ret,
};

static int __init kprobe_init(void) {
    int ret;

    ret = register_kprobe(&kp);
    if (ret < 0) {
        printk(KERN_INFO "Failed to register kprobe: %d\n", ret);
        return ret;
    }
    printk(KERN_INFO "Kprobe at %p registered\n", kp.addr);

    ret = register_kretprobe(&kr);
    if (ret < 0) {
        printk(KERN_INFO "Failed to register kretprobe: %d\n", ret);
        unregister_kprobe(&kp);  // Cleanup already registered kprobe if kretprobe registration fails
        return ret;
    }
    printk(KERN_INFO "Kretprobe registered\n");

    return 0;
}

static void __exit kprobe_exit(void) {
    unregister_kprobe(&kp);
    printk(KERN_INFO "Kprobe at %p unregistered\n", kp.addr);
    unregister_kretprobe(&kr);
    printk(KERN_INFO "Kretprobe unregistered\n");
}

module_init(kprobe_init);
module_exit(kprobe_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Your Name");
MODULE_DESCRIPTION("A simple module to monitor block IO with entry and exit tracing");
