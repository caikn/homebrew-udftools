CC ?= cc
CFLAGS ?= -O2 -Wall -Wextra
CPPFLAGS = -I. -D__APPLE__

SRCS = main.c mkudffs.c options.c defaults.c file.c \
       crc.c extent.c unicode.c misc.c

OBJS = $(SRCS:.c=.o)

TARGET = mkudffs

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CC) $(LDFLAGS) -o $@ $(OBJS)

%.o: %.c
	$(CC) $(CFLAGS) $(CPPFLAGS) -c -o $@ $<

clean:
	rm -f $(OBJS) $(TARGET)

.PHONY: all clean
