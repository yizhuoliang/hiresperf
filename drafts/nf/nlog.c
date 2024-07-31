#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/netfilter.h>
#include <linux/netfilter_ipv4.h>
#include <linux/skbuff.h>
#include <linux/ip.h>

static struct nf_hook_ops pre_routing_hook;
static struct nf_hook_ops post_routing_hook;

/* Callback function for PRE_ROUTING hook */
static unsigned int pre_routing_func(void *priv,
                                     struct sk_buff *skb,
                                     const struct nf_hook_state *state) {
    if (skb) {
        printk(KERN_INFO "PRE_ROUTING: Packet size: %u bytes\n", skb->len);
    }
    return NF_ACCEPT;
}

/* Callback function for POST_ROUTING hook */
static unsigned int post_routing_func(void *priv,
                                      struct sk_buff *skb,
                                      const struct nf_hook_state *state) {
    if (skb) {
        printk(KERN_INFO "POST_ROUTING: Packet size: %u bytes\n", skb->len);
    }
    return NF_ACCEPT;
}

/* Initialization function */
static int __init packet_logger_init(void) {
    /* Set up PRE_ROUTING hook */
    pre_routing_hook.hook = pre_routing_func;
    pre_routing_hook.hooknum = NF_INET_PRE_ROUTING;
    pre_routing_hook.pf = PF_INET;
    pre_routing_hook.priority = NF_IP_PRI_FIRST;

    /* Register PRE_ROUTING hook */
    nf_register_hook(&pre_routing_hook);

    /* Set up POST_ROUTING hook */
    post_routing_hook.hook = post_routing_func;
    post_routing_hook.hooknum = NF_INET_POST_ROUTING;
    post_routing_hook.pf = PF_INET;
    post_routing_hook.priority = NF_IP_PRI_FIRST;

    /* Register POST_ROUTING hook */
    nf_register_hook(&post_routing_hook);

    printk(KERN_INFO "Packet logger module loaded.\n");

    return 0;
}

/* Cleanup function */
static void __exit packet_logger_exit(void) {
    /* Unregister hooks */
    nf_unregister_hook(&pre_routing_hook);
    nf_unregister_hook(&post_routing_hook);

    printk(KERN_INFO "Packet logger module unloaded.\n");
}

module_init(packet_logger_init);
module_exit(packet_logger_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Your Name");
MODULE_DESCRIPTION("A simple Linux kernel module to log packet sizes at PRE_ROUTING and POST_ROUTING hooks.");
