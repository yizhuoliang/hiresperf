ROOT_PATH = ../..
CC = $(ROOT_PATH)/build/bin/clang++
LIBLDB = $(ROOT_PATH)/libldb/libldb.a


CFLAGS = -O3 -g -fdebug-default-version=3 -fno-omit-frame-pointer -I$(ROOT_PATH)/ -I$(ROOT_PATH)/libldb/include
LDFLAGS = -O3 -g -fdebug-default-version=3 -fno-omit-frame-pointer -lpthread -lnuma


all: compressor

compressor: compressor.o $(LIBLDB)
	$(CC) $(LDFLAGS) -o $@ $^

compressor.o: compressor.cc
	$(CC) $(CFLAGS) -o $@ -c $<

clean:
	rm -f $(TARGET)