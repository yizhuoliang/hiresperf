CC=gcc
BPF_CC=clang

# Source and output settings
BPF_SRC=hrp_bpf.bpf.c
BPF_OUT=hrp_bpf.o
LIB_SOURCES=log.c hrp_bpf_api.c
LIB_OBJS=log.o hrp_bpf_api.o
LIB_OUT=libhrpio.so
USER_SRC=main.c
OBJS=main.o
USER_OUT=out

# Compiler and linker flags
CFLAGS=-O2 -fPIC  # Added -fPIC for position-independent code
USER_LDFLAGS=-L. -lhrpio
BPF_CFLAGS=-O2 -g -target bpf

.PHONY: all clean lib

all: $(BPF_OUT) $(USER_OUT)

# Rule to make the BPF object file
$(BPF_OUT): $(BPF_SRC)
	$(BPF_CC) $(BPF_CFLAGS) -c $< -o $@

# Compile each source file to an object file
$(LIB_OBJS): %.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

# Rule to make the dynamic library
lib: $(LIB_OBJS)
	$(CC) -shared -o $(LIB_OUT) $(LIB_OBJS) -lbpf -pthread

# Compile main.c to an object file
$(OBJS): $(USER_SRC)
	$(CC) $(CFLAGS) -c $< -o $@

# Link the object file with the dynamic library to create the final executable
$(USER_OUT): lib $(OBJS)
	$(CC) $(OBJS) $(USER_LDFLAGS) -o $@

clean:
	rm -f $(BPF_OUT) $(USER_OUT) $(OBJS) $(LIB_OBJS) $(LIB_OUT) *.o
