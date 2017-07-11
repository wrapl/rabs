.PHONY: clean all

all: rabs ml

sources = \
	cache.c \
	context.c \
	rabs.c \
	target.c \
	util.c \
	vfs.c \
	builtins.s \
	lfs.c \
	sha256.c \
	minilang.c

CFLAGS += -I. -g
LDFLAGS += -llua -lm -lgc -lsqlite3 -lHX -g

rabs: $(sources) *.h builtins.lua
	gcc $(CFLAGS) $(sources) $(LDFLAGS) -o $@

ml: minilang.* ml.* map.* sha256.*
	gcc -g minilang.c ml.c map.c sha256.c -lgc -oml

clean:
	rm ../../bin/rabs
