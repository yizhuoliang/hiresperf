KERNELDIR ?= /lib/modules/$(shell uname -r)/build
PWD := $(shell pwd)

obj-m += hrperf.o
hrperf-objs := ./src/buffer.o ./src/log.o ./src/hrperf.o

all: submake
	$(MAKE) -C $(KERNELDIR) M=$(PWD) modules

submake:
	$(MAKE) -C ./src/io

install: all
	@mkdir -p ./install
	@cp ./hrperf.ko ./src/hrp_bpf.o ./src/libhrpio.so ./src/hrp_api.h ./install/

clean:
	$(MAKE) -C $(KERNELDIR) M=$(PWD) clean
	$(MAKE) -C ./src/io clean
	@rm -rf ./install

.PHONY: all submake install clean