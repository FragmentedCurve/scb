.PHONY: all

CC=clang
CFLAGS= -lncurses

all: scb-select

scb-select:
	$(CC) $(CFLAGS) -o $@ $@.c

clean:
	rm -f scb-select
