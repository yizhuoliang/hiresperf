#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/bio.h>
#include <linux/blkdev.h>
#include <linux/kprobes.h>

static struct kprobe kp = {
    .symbol_name    = "submit_bio",
};

static int handler_pre(struct kprobe *p, struct pt_regs *regs) {
    struct bio *bio = (struct bio *)regs->di; // For x86_64, the first argument is in DI register

    printk(KERN_INFO "submit_bio: bio=%p, sector=%llu, op=%x, size=%u, start_time=%llu\n",
           bio, (unsigned long long)bio->bi_iter.bi_sector,
           bio_op(bio), bio->bi_iter.bi_size, ktime_get_ns());

    return 0;
}

static void handler_post(struct kprobe *p, struct pt_regs *regs, unsigned long flags) {
    // Optional: handle post-submit operations
}

static int handler_fault(struct kprobe *p, struct pt_regs *regs, int trapnr) {
    printk(KERN_INFO "Fault at probe address=%p, trap number=%d\n", p->addr, trapnr);
    return 0;
}

static int __init kprobe_init(void) {
    kp.pre_handler = handler_pre;
    kp.post_handler = handler_post;
    kp.fault_handler = handler_fault;

    register_kprobe(&kp);
    printk(KERN_INFO "kprobe at %p registered\n", kp.addr);

    return 0;
}

static void __exit kprobe_exit(void) {
    unregister_kprobe(&kp);
    printk(KERN_INFO "kprobe at %p unregistered\n", kp.addr);
}

module_init(kprobe_init);
module_exit(kprobe_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Your Name");
MODULE_DESCRIPTION("A simple module to monitor block IO");
