KERNELDIR ?= /lib/modules/$(shell uname -r)/build
PWD := $(shell pwd)
BUILD_DIR := ./build

obj-m += hrperf.o
hrperf-objs := src/buffer.o src/log.o src/hrperf.o

all: build_dir
	$(MAKE) -C $(KERNELDIR) M=$(PWD)/$(BUILD_DIR) modules

clean:
	$(MAKE) -C $(KERNELDIR) M=$(PWD)/$(BUILD_DIR) clean

build_dir:
	@mkdir -p $(BUILD_DIR)
	@cp -r src/* $(BUILD_DIR)/