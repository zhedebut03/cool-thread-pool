CC=gcc
CFLAGS=-g -pedantic -std=gnu17 -Wall -Werror -Wextra -pthread
LDFLAGS='-lpthread'

.PHONY: all
all: enc

enc: enc.o threadpool.o

enc.o: enc.c threadpool.h

threadpool.o: threadpool.c threadpool.h

.PHONY: clean
clean:
	rm -f *.o enc