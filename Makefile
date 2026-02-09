# Simple Makefile for testing compilation
CC = gcc
CFLAGS = -Wall -Wextra -std=c11 -O2 -Iinclude -Isrc
LDFLAGS = -lm -lpthread

SOURCES = src/sampler.c src/envelope.c src/voice.c src/sample_loader.c src/midi_parser.c
OBJECTS = $(SOURCES:.c=.o)

all: libmidi_sampler.a simple_example

libmidi_sampler.a: $(OBJECTS)
	ar rcs $@ $^

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

simple_example: examples/simple_example.c libmidi_sampler.a
	$(CC) $(CFLAGS) $< -L. -lmidi_sampler $(LDFLAGS) -o $@

clean:
	rm -f $(OBJECTS) libmidi_sampler.a simple_example midi_player

.PHONY: all clean
