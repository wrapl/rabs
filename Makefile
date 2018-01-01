.PHONY: clean all

all: minibuild

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

CFLAGS += -std=gnu99 -I. -Iminilang -g -pthread -DGC_THREADS -D_GNU_SOURCE
LDFLAGS += -lm -ldl -lsqlite3 -g -lgc

minibuild: Makefile $(sources) *.h
	gcc $(CFLAGS) $(sources) $(LDFLAGS) -o $@

clean:
	rm minibuild
