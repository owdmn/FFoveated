#### Configurable paths:
FFMPEG = ../lib/ffmpeg-foveated/
####

CPFLAGS = --no-tree --no-signoff -f --ignore NEW_TYPEDEFS,AVOID_EXTERNS,SPDX_LICENSE_TAG,CONST_STRUCT
CC = gcc
CFLAGS= -I$(FFMPEG) -Wall -Wextra -Wpedantic -g
LDFLAGS= -L$(FFMPEG) -lavutil -lavcodec -lavdevice -lavformat -lSDL2 -lm -g

.PHONY: clean checkpatch

all: main

eyetracking: CFLAGS += -DET
eyetracking: main

debug: CFLAGS += -g -pg -DDEBUG
debug: LDFLAGS += -pg
debug: main

%.o: %.c
	$(CC) -c $(CFLAGS) -o $@ $<

main: io.o codec.o et.o main.o pexit.o queue.o window.o
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

replicate: replicate.o io.o codec.o et.o pexit.o queue.o
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

checkpatch:
	perl $(CHECKPATCH) $(CPFLAGS) *.c *.h

clean:
	rm -f main replicate *.o *.out

