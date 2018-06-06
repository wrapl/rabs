.PHONY: clean all

all: rabs

sources = \
	minilang/sha256.c \
	minilang/minilang.c \
	minilang/ml_file.c \
	minilang/stringmap.c \
	minilang/linenoise.c \
	cache.c \
	context.c \
	rabs.c \
	target.c \
	util.c \
	vfs.c

VERSION = test

CFLAGS += -std=gnu99 -I. -Iminilang -pthread -DGC_THREADS -D_GNU_SOURCE -O3
LDFLAGS += -lm -ldl -lsqlite3 -lgc

rabs: Makefile $(sources) *.h
	gcc $(CFLAGS) $(sources) $(LDFLAGS) -o $@

clean:
	rm rabs
