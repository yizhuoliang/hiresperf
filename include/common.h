#ifndef COMMON_H
#define COMMON_H

#include <linux/ioctl.h>
#include <linux/stddef.h>
#include <linux/types.h>
#include <linux/align.h>

#define CACHE_LINE_SIZE 64
#define ALIGN_TO_CACHE_LINE __aligned(CACHE_LINE_SIZE)

#endif // COMMON_H