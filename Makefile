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

CFLAGS += -std=gnu99 -I. -Iminilang -pthread -DSQLITE_THREADSAFE=0 -DGC_THREADS -D_GNU_SOURCE -flto -O3 -g
LDFLAGS += -lm -ldl -lgc -lsqlite3 -g -flto

rabs: Makefile $(sources) *.h
	gcc $(CFLAGS) $(sources) $(LDFLAGS) -o $@

clean:
	rm rabs
