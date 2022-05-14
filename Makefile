.PHONY: all

CFLAGS= -lncurses -O2 -Wall -Werror

all: scb

scb: scb.c
	$(CC) $(CFLAGS) -o $@ $@.c

clean:
	rm -f scb
