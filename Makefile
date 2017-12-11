.PHONY: clean all

all: minilang minibuild

sources = \
	cache.c \
	context.c \
	rabs.c \
	target.c \
	util.c \
	vfs.c \
	sha256.c \
	minilang.c \
	ml_file.c \
	stringmap.c \
	linenoise.c

CFLAGS += -I. -Igc/include -g -pthread
LDFLAGS += -lm -ldl -lsqlite3 -g gc/lib/libgc.a

minibuild: $(sources) *.h
	gcc $(CFLAGS) $(sources) $(LDFLAGS) -o $@
	
minilang: minilang.* ml.* stringmap.* sha256.*
	gcc $(CFLAGS) minilang.c ml_file.c ml.c stringmap.c sha256.c linenoise.c -lgc -o$@

clean:
	rm minilang
	rm minibuild
