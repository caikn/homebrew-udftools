CC = cc
CFLAGS = -Wall -Wextra -O2 -std=c99 -D_FILE_OFFSET_BITS=64 -I../include -I../mkudffs
LDFLAGS =

SRCS = main.c pack.c
OBJS = $(SRCS:.c=.o)
MKUDFFS_OBJS = mkudffs_lib.o defaults.o file_lib.o
LIB_OBJS = extent.o unicode.o crc.o misc.o
TARGET = mkbdrom

all: $(TARGET)

$(TARGET): $(OBJS) $(MKUDFFS_OBJS) $(LIB_OBJS)
	$(CC) $(LDFLAGS) -o $@ $^

main.o: main.c ../include/config.h ../include/libudffs.h ../mkudffs/mkudffs.h ../mkudffs/file.h pack.h
	$(CC) $(CFLAGS) -c -o $@ $<

pack.o: pack.c ../include/config.h ../include/libudffs.h ../mkudffs/file.h pack.h
	$(CC) $(CFLAGS) -c -o $@ $<

mkudffs_lib.o: ../mkudffs/mkudffs.c ../include/config.h ../include/libudffs.h ../mkudffs/mkudffs.h
	$(CC) $(CFLAGS) -c -o $@ $<

defaults.o: ../mkudffs/defaults.c ../include/config.h ../include/libudffs.h ../mkudffs/mkudffs.h
	$(CC) $(CFLAGS) -c -o $@ $<

file_lib.o: ../mkudffs/file.c ../include/config.h ../include/libudffs.h ../mkudffs/file.h
	$(CC) $(CFLAGS) -c -o $@ $<

extent.o: ../libudffs/extent.c ../include/config.h ../include/libudffs.h
	$(CC) $(CFLAGS) -c -o $@ $<

unicode.o: ../libudffs/unicode.c ../include/config.h ../include/libudffs.h
	$(CC) $(CFLAGS) -c -o $@ $<

crc.o: ../libudffs/crc.c ../include/config.h ../include/libudffs.h
	$(CC) $(CFLAGS) -c -o $@ $<

misc.o: ../libudffs/misc.c ../include/config.h ../include/libudffs.h
	$(CC) $(CFLAGS) -c -o $@ $<

clean:
	rm -f $(OBJS) $(MKUDFFS_OBJS) $(LIB_OBJS) $(TARGET)

.PHONY: all clean
