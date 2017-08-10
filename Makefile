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
	ml_file.c \
	stringmap.c

CFLAGS += -I. -g
LDFLAGS += -lm -lgc -lsqlite3 -g

minibuild: $(sources) *.h
	gcc $(CFLAGS) $(sources) $(LDFLAGS) -o $@
	
minilang: minilang.* ml.* stringmap.* sha256.*
	gcc $(CFLAGS) minilang.c ml_file.c ml.c stringmap.c sha256.c -lgc -o$@

clean:
	rm minilang
	rm minibuild
