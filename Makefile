KERNELDIR   ?= /lib/modules/$(shell uname -r)/build
MODULE_NAME := hrperf

obj-m := $(MODULE_NAME).o
$(MODULE_NAME)-objs := \
	./src/buffer.o \
	./src/log.o \
	./src/hrperf.o \
	./src/cpucounters.o \
	./src/mmio.o \
	./src/uncore_pmu_discovery.o \
	./src/uncore_pmu.o \
	./src/mbm/rmid.o \
	./src/mbm/mbm.o

ccflags-y := -I$(PWD)/include -Wall -g

.PHONY: all submake install clean workloads
	

all: submake workloads
	$(MAKE) -C $(KERNELDIR) M=$(PWD) modules

workloads: 
	$(MAKE) -C ./workloads

submake:
	# io monitor via bpf is still experimental
	# $(MAKE) -C ./src/io

install: all
	@mkdir -p ./install
	@cp ./hrperf.ko ./src/hrperf_api.h ./src/hrperf_api.py ./install/
	# @cp ./src/io/hrp_bpf.o ./src/io/libhrpio.so ./install/

clean:
	$(MAKE) -C $(KERNELDIR) M=$(PWD) clean
	$(MAKE) -C ./src/io clean
	$(MAKE) -C ./workloads clean
	rm -rf ./install
	rm -f .*.cmd *.mod.c Module.symvers modules.order
