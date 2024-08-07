CC=gcc
BPF_CC=clang
# This is for most BPF operations
BPF_HEADER_PATH=/usr/include/linux/bpf
# This is specifically for the block IO related headers
KERNEL_HEADERS=/usr/src/linux-headers-5.15.0-113/include

BPF_SRC=hrp_bpf.bpf.c
BPF_OUT=hrp_bpf.o

USER_SRC=main.c consumer.c log.c
OBJS=main.o consumer.o log.o
USER_OUT=out

CFLAGS=-O2 -g
USER_LDFLAGS=-lbpf -pthread
BPF_CFLAGS=-O2 -g -target bpf -I$(BPF_HEADER_PATH) -I$(KERNEL_HEADERS)

.PHONY: all clean

all: $(BPF_OUT) $(USER_OUT)

$(BPF_OUT): $(BPF_SRC)
	$(BPF_CC) $(BPF_CFLAGS) -c $< -o $@

# Compile each source file to an object file
$(OBJS): %.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

# Link all object files to create the final executable
$(USER_OUT): $(OBJS)
	$(CC) $(OBJS) $(USER_LDFLAGS) -o $@

clean:
	rm -f $(BPF_OUT) $(USER_OUT) $(OBJS)
