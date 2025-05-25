KERNELDIR ?= /lib/modules/$(shell uname -r)/build
PWD := $(shell pwd)

obj-m += hrperf.o
hrperf-objs := ./src/buffer.o ./src/log.o ./src/hrperf.o

all: submake
	$(MAKE) -C $(KERNELDIR) M=$(PWD) modules

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
	@rm -rf ./install

.PHONY: all submake install clean