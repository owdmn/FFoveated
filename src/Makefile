CC = gcc
CFLAGS= -I/path/to/FFmpeg/**
LDFLAGS= -L/path/to/FFmpeg/** -lavutil -lavcodec -lavdevice

.PHONY: clean

%.o: %.c $(DEPS)
	$(CC) -g -c -o $@ $<  $(CFLAGS)

main: main.o
	$(CC) -g -o $@ $^ $(CFLAGS) $(LDFLAGS)


clean:
	rm *.o main


