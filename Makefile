.PHONY: clean all

all: rabs

minilang/libminilang.a: minilang/Makefile
	make -C minilang libminilang.a

*.o: *.h minilang/*.h

objects = \
	cache.o \
	context.o \
	rabs.o \
	target.o \
	targetcache.o \
	targetset.o \
	util.o \
	vfs.o

CFLAGS += -std=gnu11 -fstrict-aliasing -Wstrict-aliasing -I. -Iminilang -pthread -DSQLITE_THREADSAFE=0 -DGC_THREADS -D_GNU_SOURCE
LDFLAGS += -lm -ldl -lgc -lsqlite3 minilang/libminilang.a

ifdef DEBUG
	CFLAGS += -g
	LDFLAGS += -g
else
	CFLAGS += -O2
endif

rabs: Makefile $(objects) *.h
	gcc $(objects) $(LDFLAGS) -o $@

clean:
	rm rabs
