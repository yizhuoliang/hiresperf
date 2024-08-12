KERNELDIR ?= /lib/modules/$(shell uname -r)/build
BUILD_DIR := build

obj-m += hrperf.o
hrperf-objs := ./src/buffer.o ./src/log.o ./src/hrperf.o

all:
	$(MAKE) -C $(KERNELDIR) M=$(BUILD_DIR) modules

clean:
	$(MAKE) -C $(KERNELDIR) M=$(BUILD_DIR) clean