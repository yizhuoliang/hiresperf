# Makefile for dedup kernel

PREFIX=${PARSECDIR}/pkgs/kernels/dedup/inst/${PARSECPLAT}

TARGET=dedup

# Define compilers
CC := ~/llvm14-ldb/build/bin/clang
GCC := /usr/bin/gcc

CFLAGS += -Wall -fno-strict-aliasing -D_XOPEN_SOURCE=600

ostype=$(findstring solaris, ${PARSECPLAT})

ifeq "$(ostype)" "solaris"
	CFLAGS += -std=gnu99
endif

LIBS += -lm

DEDUP_OBJ = hashtable.o util.o dedup.o rabin.o encoder.o decoder.o mbuffer.o sha.o

# Uncomment the following to enable gzip compression
CFLAGS += -DENABLE_GZIP_COMPRESSION
LIBS += -lz

# Uncomment the following to enable bzip2 compression
#CFLAGS += -DENABLE_BZIP2_COMPRESSION
#LIBS += -lbz2

ifdef version
  ifeq "$(version)" "pthreads"
	CFLAGS += -DENABLE_PTHREADS -pthread
	DEDUP_OBJ += queue.o binheap.o tree.o
  endif
endif

# Remove -fdebug-default-version=3 from CFLAGS for gcc
CFLAGS_GCC := $(filter-out -fdebug-default-version=3, $(CFLAGS))

all: $(TARGET)

# Default rule for compiling .c files with clang
%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

# Specific rule for compiling sha.o with gcc, using modified CFLAGS
sha.o: sha.c
	$(GCC) $(CFLAGS_GCC) -c $< -o $@

$(TARGET): $(DEDUP_OBJ)
	$(CC) $(CFLAGS) $(LDFLAGS) -o $(TARGET) $(DEDUP_OBJ) $(LIBS)

clean:
	rm -f *~ *.o $(TARGET)

install:
	mkdir -p $(PREFIX)/bin
	cp -f $(TARGET) $(PREFIX)/bin/$(TARGET)
