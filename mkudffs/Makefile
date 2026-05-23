CC ?= cc
CFLAGS ?= -O2 -Wall -Wextra
CPPFLAGS = -I. -I../include -D__APPLE__

SRCS = main.c mkudffs.c options.c defaults.c file.c
LIB_SRCS = ../libudffs/crc.c ../libudffs/extent.c ../libudffs/unicode.c ../libudffs/misc.c

OBJS = $(SRCS:.c=.o)
LIB_OBJS = crc.o extent.o unicode.o misc.o

TARGET = mkudffs

all: $(TARGET)

$(TARGET): $(OBJS) $(LIB_OBJS)
	$(CC) $(LDFLAGS) -o $@ $^

%.o: %.c
	$(CC) $(CFLAGS) $(CPPFLAGS) -c -o $@ $<

crc.o: ../libudffs/crc.c
	$(CC) $(CFLAGS) $(CPPFLAGS) -c -o $@ $<

extent.o: ../libudffs/extent.c
	$(CC) $(CFLAGS) $(CPPFLAGS) -c -o $@ $<

unicode.o: ../libudffs/unicode.c
	$(CC) $(CFLAGS) $(CPPFLAGS) -c -o $@ $<

misc.o: ../libudffs/misc.c
	$(CC) $(CFLAGS) $(CPPFLAGS) -c -o $@ $<

clean:
	rm -f $(OBJS) $(LIB_OBJS) $(TARGET)

.PHONY: all clean
