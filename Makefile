.PHONY: clean all

all: minilang minibuild

sources = \
	cache.c \
	context.c \
	rabs.c \
	target.c \
	util.c \
	vfs.c \
	builtins.s \
	sha256.c \
	minilang.c \
	stringmap.c \
	stringbuffer.c \
	extras.c

CFLAGS += -I. -g
LDFLAGS += -lm -lgc -lsqlite3 -lHX -g

minibuild: $(sources) *.h
	gcc $(CFLAGS) $(sources) $(LDFLAGS) -o $@
	
minilang: minilang.* ml.* stringmap.* sha256.*
	gcc $(CFLAGS) minilang.c ml.c stringmap.c sha256.c extras.c stringbuffer.c -lgc -o$@

clean:
	rm minilang
	rm minibuild
