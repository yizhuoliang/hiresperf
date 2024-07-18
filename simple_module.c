// simple_module.c
#include <linux/init.h>
#include <linux/module.h>
#include <linux/device.h>
#include <linux/kernel.h>
#include <linux/fs.h>
#include <linux/uaccess.h>

#define DEVICE_NAME "simplechar"
#define CLASS_NAME "simple"

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Your Name");
MODULE_DESCRIPTION("A simple Linux char driver");

static int majorNumber;
static char message[256] = {0};
static short size_of_message;
static struct class* simpleClass = NULL;
static struct device* simpleDevice = NULL;

static int dev_open(struct inode *, struct file *);
static int dev_release(struct inode *, struct file *);
static ssize_t dev_read(struct file *, char *, size_t, loff_t *);

static struct file_operations fops =
{
   .open = dev_open,
   .read = dev_read,
   .release = dev_release,
};

static int __init simple_init(void) {
    printk(KERN_INFO "SimpleChar: Initializing the SimpleChar LKM\n");

    majorNumber = register_chrdev(281, DEVICE_NAME, &fops);
    if (majorNumber < 0) {
        printk(KERN_ALERT "SimpleChar failed to register a major number\n");
        return majorNumber;
    }
    printk(KERN_INFO "SimpleChar: registered correctly with major number %d\n", majorNumber);

    simpleClass = class_create(THIS_MODULE, CLASS_NAME);
    if (IS_ERR(simpleClass)) {
        unregister_chrdev(majorNumber, DEVICE_NAME);
        printk(KERN_ALERT "Failed to register device class\n");
        return PTR_ERR(simpleClass);
    }
    printk(KERN_INFO "SimpleChar: device class registered correctly\n");

    simpleDevice = device_create(simpleClass, NULL, MKDEV(majorNumber, 0), NULL, DEVICE_NAME);
    if (IS_ERR(simpleDevice)) {
        class_destroy(simpleClass);
        unregister_chrdev(majorNumber, DEVICE_NAME);
        printk(KERN_ALERT "Failed to create the device\n");
        return PTR_ERR(simpleDevice);
    }
    printk(KERN_INFO "SimpleChar: device class created correctly\n");
    return 0;
}

static void __exit simple_exit(void) {
    device_destroy(simpleClass, MKDEV(majorNumber, 0));
    class_unregister(simpleClass);
    class_destroy(simpleClass);
    unregister_chrdev(majorNumber, DEVICE_NAME);
    printk(KERN_INFO "SimpleChar: Goodbye from the LKM!\n");
}

static int dev_open(struct inode *inodep, struct file *filep) {
   printk(KERN_INFO "SimpleChar: Device has been opened\n");
   return 0;
}

static ssize_t dev_read(struct file *filep, char *buffer, size_t len, loff_t *offset) {
    int error_count = 0;
    error_count = copy_to_user(buffer, message, size_of_message);

    if (error_count==0) {
        printk(KERN_INFO "SimpleChar: Sent %d characters to the user\n", size_of_message);
        return (size_of_message=0);
    } else {
        printk(KERN_INFO "SimpleChar: Failed to send %d characters to the user\n", error_count);
        return -EFAULT;
    }
}

static int dev_release(struct inode *inodep, struct file *filep) {
   printk(KERN_INFO "SimpleChar: Device successfully closed\n");
   return 0;
}

module_init(simple_init);
module_exit(simple_exit);
