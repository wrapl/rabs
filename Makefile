.PHONY: clean all

all: ../../bin/rabs

sources = \
	cache.c \
	context.c \
	rabs.c \
	target.c \
	util.c \
	vfs.c \
	builtins.s \
	lfs.c \
	sha256.c

CFLAGS += -I. -g
LDFLAGS += -llua -lm -lgc -lsqlite3 -lHX -g

../../bin/rabs: $(sources) *.h builtins.lua
	gcc $(CFLAGS) $(sources) $(LDFLAGS) -o $@

clean:
	rm ../../bin/rabs
