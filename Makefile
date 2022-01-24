SRC := $(wildcard *.c)
OBJS = $(SRC:.c=.o)
CC=gcc
CFLAGS := -Wall -O3 -g -pthread -fmessage-length=0 $(shell mysql_config --cflags)
LIBS = -lev -Wl,-rpath=/usr/local/lib $(shell mysql_config --libs)

TARGET	= udp-proxy

%.o: %.c
	$(CC) $(CFLAGS) -c -o $@ $<

.PHONY : all clean

all:	$(TARGET)

udp-proxy:	$(OBJS)
	$(CC) $(CFLAGS) -o $@ $^ $(LIBS)

clean:
	rm -f $(TARGET) $(OBJS)
