#include <linux/cdev.h>
#include <linux/device.h>
#include <linux/fs.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/uaccess.h>  // for copy_to_user

#include "pmc.h"
#include "intel_msr.h"

#define DEVICE_NAME "hellochar"
#define CLASS_NAME "hello"

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Your Name");
MODULE_DESCRIPTION("A simple Linux char driver for Hello World");

static int majorNumber;
static struct class* helloClass = NULL;
static struct device* helloDevice = NULL;
static struct cdev helloCDev;

static int dev_open(struct inode *, struct file *);
static ssize_t dev_read(struct file *, char *, size_t, loff_t *);

static struct file_operations fops =
{
   .open = dev_open,
   .read = dev_read,
};

static void __init ksched_init_pmc(void *arg)
{
	wrmsrl(MSR_IA32_FIXED_CTR_CTRL, 0x030);
	wrmsrl(MSR_IA32_GLOBAL_CTRL,
	       (0UL) |
	       (1UL) |
	       (1UL << 32) | (1UL << 33) | (1UL << 34));
}

static void ksched_stop_pmc(void *arg)
{   
    // Zero all the control MSRs to stop counters
    wrmsrl(MSR_IA32_FIXED_CTR_CTRL, 0UL);
    wrmsrl(MSR_IA32_GLOBAL_CTRL, 0UL);
}

static u64 ksched_measure_pmc(u64 sel)
{
	u64 val;
    wrmsrl(MSR_IA32_PERFEVTSEL0, sel);
	rdmsrl(MSR_IA32_PMC0, val);

	return val;
}

static u64 ksched_measure_pmc1(u64 sel)
{
	u64 val;
    wrmsrl(MSR_IA32_PERFEVTSEL1, sel);
	rdmsrl(MSR_IA32_PMC1, val);

	return val;
}

static u64 measure_pmc_fixed_1(void)
{
    u64 val;
    rdmsrl(MSR_IA32_FIXED_CTR1, val);
    return val;
}

static int __init hellochar_init(void) {
    printk(KERN_INFO "HelloChar: Initializing the HelloChar LKM\n");

    dev_t dev_num = MKDEV(280, 0);
    if (register_chrdev_region(dev_num, 1, DEVICE_NAME) < 0) {
        printk(KERN_ALERT "HelloChar failed to register a major number\n");
        return -1;
    }
    majorNumber = MAJOR(dev_num);
    printk(KERN_INFO "HelloChar: registered with major number %d\n", majorNumber);

    cdev_init(&helloCDev, &fops);
    helloCDev.owner = THIS_MODULE;
    if (cdev_add(&helloCDev, dev_num, 1) < 0) {
        unregister_chrdev_region(dev_num, 1);
        printk(KERN_ALERT "HelloChar failed to add cdev\n");
        return -1;
    }

    helloClass = class_create(THIS_MODULE, CLASS_NAME);
    if (IS_ERR(helloClass)) {
        unregister_chrdev_region(dev_num, 1);
        cdev_del(&helloCDev);
        printk(KERN_ALERT "Failed to register device class\n");
        return PTR_ERR(helloClass);
    }
    printk(KERN_INFO "HelloChar: device class registered correctly\n");

    helloDevice = device_create(helloClass, NULL, dev_num, NULL, DEVICE_NAME);
    if (IS_ERR(helloDevice)) {
        class_destroy(helloClass);
        cdev_del(&helloCDev);
        unregister_chrdev_region(dev_num, 1);
        printk(KERN_ALERT "Failed to create the device\n");
        return PTR_ERR(helloDevice);
    }

    ksched_init_pmc(NULL);

    printk(KERN_INFO "HelloChar: device class created correctly\n");
    return 0;
}

static void __exit hellochar_exit(void) {
    device_destroy(helloClass, MKDEV(majorNumber, 0));
    class_unregister(helloClass);
    class_destroy(helloClass);
    cdev_del(&helloCDev);
    unregister_chrdev_region(MKDEV(majorNumber, 0), 1);
    ksched_stop_pmc(NULL);
    printk(KERN_INFO "HelloChar: Goodbye from the LKM!\n");
}

static int dev_open(struct inode *inodep, struct file *filep) {
    printk(KERN_INFO "HelloChar: Device has been opened\n");
    return 0;
}

unsigned long long read_tsc(void) {
    unsigned int lo, hi;
    asm volatile ("rdtsc" : "=a" (lo), "=d" (hi));
    return ((unsigned long long)hi << 32) | lo;
}

static ssize_t dev_read(struct file *filep, char *buffer, size_t len, loff_t *offset) {
    static const char *message = "Hello world from LKM!";
    int error_count = 0;
    int message_len = strlen(message);

    // Only allow reading the whole message, not part of it
    if (*offset > 0 || len < message_len) {
        return 0;
    }

    error_count = copy_to_user(buffer, message, message_len);
    unsigned long long res_tsc = read_tsc();
    unsigned long long res_llc = (unsigned long long) ksched_measure_pmc(PMC_LLC_MISSES);
    unsigned long long res_prf = (unsigned long long) ksched_measure_pmc1(PMC_SW_PREFETCH_ANY_ESEL);
    unsigned long long res_cpu = (unsigned long long) measure_pmc_fixed_1();

    printk(KERN_INFO "HelloCahr: TSC: %llu LLC Miss: %llu, Prefetch: %llu, CPU UNHALT: %llu", res_tsc, res_llc, res_prf, res_cpu);

    if (error_count == 0) {
        printk(KERN_INFO "HelloChar: Sent %d characters to the user\n", message_len);
        *offset += message_len;  // Update the offset to prevent re-reading
        return message_len;      // Return the number of characters read
    } else {
        printk(KERN_ALERT "HelloChar: Failed to send %d characters to the user\n", error_count);
        return -EFAULT;          // Bad address message
    }
}

module_init(hellochar_init);
module_exit(hellochar_exit);
