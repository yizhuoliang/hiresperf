CC = gcc
CFLAGS = -O0 -g -pthread -D_GNU_SOURCE

# Sources: all .c files in the current directory
SOURCES = $(wildcard *.c)

# Targets: all .c files without the .c extension
TARGETS = $(SOURCES:.c=)

all: $(TARGETS)

%: %.c
	$(CC) $(CFLAGS) -o $@ $<

clean:
	rm -f $(TARGETS)