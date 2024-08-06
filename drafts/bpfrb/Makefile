CC=gcc
BPF_CC=clang

BPF_SRC=hrp_bpf.bpf.c
BPF_OUT=hrp_bpf.o

USER_SRC=consumer.c log.c
USER_OUT=consumer

CFLAGS=-O2 -g
LDFLAGS=-lbpf
BPF_CFLAGS=-O2 -g -target bpf

.PHONY: all clean

all: $(BPF_OUT) $(USER_OUT)

$(BPF_OUT): $(BPF_SRC)
	$(BPF_CC) $(BPF_CFLAGS) -c $< -o $@

$(USER_OUT): $(USER_SRC)
	$(CC) $(CFLAGS) $^ $(LDFLAGS) -o $@

clean:
	rm -f $(BPF_OUT) $(USER_OUT)
