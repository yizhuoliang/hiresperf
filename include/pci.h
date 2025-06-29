/*
 * This file is modified from Intel PCM project
 * The original file is `pci.h`.
 *
 * Modified by:
 *  - Yibo Yan
*/

#ifndef PCI_H
#define PCI_H

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/pci.h>
#include <linux/types.h>

/* The vendor ID for Intel devices */
#define PCM_INTEL_PCI_VENDOR_ID 0x8086

/**
 * @brief Represents the layout of a Vendor-Specific Extended Capability (VSEC).
 *
 * This union allows accessing the VSEC data either as a structured set of
 * bitfields or as raw 32/64-bit values. The layout must match the hardware
 * specification.
 */
union pcm_vsec {
    struct {
        u64 cap_id       : 16;
        u64 cap_version  : 4;
        u64 cap_next     : 12;
        u64 vsec_id      : 16;
        u64 vsec_version : 4;
        u64 vsec_length  : 12;
        u64 entryID      : 16;
        u64 NumEntries   : 8;
        u64 EntrySize    : 8;
        u64 tBIR         : 3;
        u64 Address      : 29;
    } fields;

    u64 raw_value64[2];
    u32 raw_value32[4];
};

/**
 * @brief Scans all Intel PCI devices for a matching DVSEC and processes it.
 *
 * This function iterates through every PCI device on the system. For each
 * device manufactured by Intel, it walks the extended capability list.
 * If a Vendor-Specific Extended Capability (VSEC) is found, it calls the
 * user-provided matchFunc. If matchFunc returns true, it calls the
 * user-provided processFunc.
 *
 * @param matchFunc   A pointer to a function that takes a VSEC header and
 * returns true (non-zero) for a match, or false (0) otherwise.
 * @param processFunc A pointer to a function that will be called with the
 * pci_dev struct, the calculated BAR address, and the
 * matching VSEC header.
 * @param priv A private context pointer to be passed to processFunc.
 */
static inline void
pcm_process_dvsec(bool (*matchFunc)(const union pcm_vsec* vsec),
                  void (*processFunc)(struct pci_dev* dev, u64 bar_addr,
                                      const union pcm_vsec* vsec, void* priv),
                  void* priv) {
    struct pci_dev* dev = NULL;
    int cap_offset;

    /*
	 * Iterate over every PCI device known to the kernel.
	 * pci_get_device will increment the device's reference count, which
	 * we must release with pci_dev_put().
	 */
    for_each_pci_dev(dev) {
        union pcm_vsec vsec;
        u64 bar_addr;
        u32 tbir;

        if (dev->vendor != PCM_INTEL_PCI_VENDOR_ID) {
            continue;
        }

        // pr_info("kimc: Found Intel device %s\n", pci_name(dev));

        /*
		 * Find the first extended capability. pci_find_ext_capability
		 * is the standard kernel way to walk the capability list.
		 */
        cap_offset = pci_find_ext_capability(dev, PCI_EXT_CAP_ID_DVSEC);

        while (cap_offset) {
            /* Read the 16-byte VSEC header */
            pci_read_config_dword(dev, cap_offset, &vsec.raw_value32[0]);
            pci_read_config_dword(dev, cap_offset + 4, &vsec.raw_value32[1]);
            pci_read_config_dword(dev, cap_offset + 8, &vsec.raw_value32[2]);
            pci_read_config_dword(dev, cap_offset + 12, &vsec.raw_value32[3]);

            /*
			 * If the header is invalid (all zeros), we've likely reached
			 * the end of the list or an unpopulated entry.
			 */
            if (vsec.raw_value64[0] == 0 && vsec.raw_value64[1] == 0) {
                break;
            }

            // pr_info("kimc: %s: VSEC@0x%x ID=0x%x EntryID=0x%x\n",
            //          pci_name(dev), cap_offset, vsec.fields.vsec_id,
            //          vsec.fields.entryID);

            /*
			 * Call the user-provided callback to check if this VSEC
			 * is the one we are looking for.
			 */
            if (matchFunc(&vsec)) {
                pr_debug("kimc: %s: Found matching VSEC\n", pci_name(dev));

                tbir = vsec.fields.tBIR;
                /* tBIR is an index into the BAR array (0-5) */
                if (tbir > PCI_STD_RESOURCE_END) {
                    pr_warn("kimc: %s: Invalid tBIR value %u\n", pci_name(dev),
                            tbir);
                    break;
                }

                /*
				 * Get the physical base address from the BAR.
				 * pci_resource_start is the kernel-native way to get this.
				 */
                bar_addr = pci_resource_start(dev, tbir);
                if (bar_addr == 0) {
                    pr_warn("kimc: %s: BAR %u is not configured or invalid\n",
                            pci_name(dev), tbir);
                    break;
                }

                /* The address in the VSEC is an offset from the BAR */
                bar_addr += vsec.fields.Address;

                /* Call the user-provided processing function */
                processFunc(dev, bar_addr, &vsec, priv);
            }

            /* Find the next Vendor-Specific capability */
            cap_offset = pci_find_next_ext_capability(dev, cap_offset,
                                                      PCI_EXT_CAP_ID_DVSEC);
        }
    }
}

#endif // PCI_H