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

CFLAGS += -std=gnu99 -fstrict-aliasing -Wstrict-aliasing -O2 -I. -Iminilang -pthread -DSQLITE_THREADSAFE=0 -DGC_THREADS -D_GNU_SOURCE -g
LDFLAGS += -lm -ldl -lgc -lsqlite3 -g

rabs: Makefile $(sources) *.h
	gcc $(CFLAGS) $(sources) $(LDFLAGS) -o $@

clean:
	rm rabs
