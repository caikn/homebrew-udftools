CC = cc
CFLAGS = -Wall -Wextra -O2 -std=c99 -D_FILE_OFFSET_BITS=64 -I../include
LDFLAGS =

SRCS = main.c options.c readdisc.c
OBJS = $(SRCS:.c=.o)
LIB_OBJS = extent.o unicode.o crc.o misc.o
TARGET = udfinfo

all: $(TARGET)

$(TARGET): $(OBJS) $(LIB_OBJS)
	$(CC) $(LDFLAGS) -o $@ $^

main.o: main.c ../include/config.h ../include/libudffs.h options.h readdisc.h
	$(CC) $(CFLAGS) -c -o $@ $<

options.o: options.c ../include/config.h ../include/libudffs.h options.h
	$(CC) $(CFLAGS) -c -o $@ $<

readdisc.o: readdisc.c ../include/config.h ../include/libudffs.h readdisc.h
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
	rm -f $(OBJS) $(LIB_OBJS) $(TARGET)

.PHONY: all clean
