CC=gcc
BPF_CC=clang

# Paths for BPF and kernel headers
BPF_HEADER_PATH=/usr/include/linux
KERNEL_HEADERS=/usr/src/linux-headers-5.15.0-113/include

# Source and Output Definitions
BPF_SRC=hrp_bpf.bpf.c
BPF_OUT=hrp_bpf.o
LIB_SRC=log.c hrp_bpf_api.c
LIB_OBJS=log.o hrp_bpf_api.o
LIB_OUT=libhrpio.a
USER_SRC=main.c
USER_OBJS=main.o
USER_OUT=out

# Compiler Flags
CFLAGS=-O2 -g -I.  # Add -I. to include the current directory for headers
USER_LDFLAGS=-lbpf -pthread -L. -lhrpio  # Add -L. -lhrpio to link with libhrpio
BPF_CFLAGS=-O2 -g -target bpf

.PHONY: all clean

all: $(BPF_OUT) $(LIB_OUT) $(USER_OUT)

# BPF Compilation
$(BPF_OUT): $(BPF_SRC)
	$(BPF_CC) $(BPF_CFLAGS) -I$(BPF_HEADER_PATH) -c $< -o $@

# Static Library Compilation
$(LIB_OBJS): %.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

$(LIB_OUT): $(LIB_OBJS)
	ar rcs $@ $^

# Main Application Compilation
$(USER_OBJS): %.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

$(USER_OUT): $(USER_OBJS)
	$(CC) $(USER_OBJS) $(USER_LDFLAGS) -o $@

clean:
	rm -f $(BPF_OUT) $(USER_OUT) $(LIB_OBJS) $(USER_OBJS) $(LIB_OUT)
