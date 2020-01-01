#### Configurable paths:
FFMPEG = /path/to/FFmpeg/**
CHECKPATCH = /path/to/checkpatch.pl
####

CPFLAGS = --no-tree --no-signoff -f --ignore NEW_TYPEDEFS,AVOID_EXTERNS,SPDX_LICENSE_TAG,CONST_STRUCT
CC = gcc
CFLAGS= -I$(FFMPEG) -Wall -Wextra -Wpedantic
LDFLAGS= -L$(FFMPEG) -lavutil -lavcodec -lavdevice -lavformat -lSDL2

.PHONY: clean checkpatch

all: debug

eyetracking: CFLAGS += -DET
eyetracking: main

debug: CFLAGS += -g -pg -DDEBUG
debug: LDFLAGS += -pg
debug: main

%.o: %.c
	$(CC) -c $(CFLAGS) -o $@ $<

main: io.o codec.o et.o main.o
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

checkpatch:
	perl $(CHECKPATCH) $(CPFLAGS) *.c *.h

clean:
	rm -f main *.o *.out

