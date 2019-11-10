# Path to FFmpeg
FFMPEG = /path/to/FFmpeg/**

CC = gcc
CFLAGS= -I$(FFMPEG) -Wall -Wextra -Wpedantic
LDFLAGS= -L$(FFMPEG) -lavutil -lavcodec -lavdevice

.PHONY: clean

all: main

debug: CFLAGS += -g -pg
debug: LDFLAGS += -pg
debug: all

%.o: %.c
	$(CC) -c $(CFLAGS) -o $@ $<

main: main.o
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

clean:
	rm main *.o *.out

