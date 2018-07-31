.PHONY: clean all

all: rabs

sources = \
	minilang/sha256.c \
	minilang/minilang.c \
	minilang/ml_compiler.c \
	minilang/ml_runtime.c \
	minilang/ml_types.c \
	minilang/ml_file.c \
	minilang/ml_console.c \
	minilang/stringmap.c \
	minilang/linenoise.c \
	cache.c \
	context.c \
	rabs.c \
	target.c \
	targetset.c \
	targetcache.c \
	util.c \
	vfs.c

CFLAGS += -std=gnu11 -fstrict-aliasing -Wstrict-aliasing -I. -Iminilang -pthread -DSQLITE_THREADSAFE=0 -DGC_THREADS -D_GNU_SOURCE
LDFLAGS += -lm -ldl -lgc -lsqlite3

ifdef DEBUG
	CFLAGS += -g
	LDFLAGS += -g
else
	CFLAGS += -O2
endif

rabs: Makefile $(sources) *.h
	gcc $(CFLAGS) $(sources) $(LDFLAGS) -o $@

clean:
	rm rabs
