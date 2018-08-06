.PHONY: clean all install

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

rabs: Makefile $(objects) *.h minilang/libminilang.a
	gcc $(objects) $(LDFLAGS) -o $@

clean:
	rm -f rabs
	rm -f *.o

PREFIX = /usr
install_bin = $(PREFIX)/bin

install_exe = $(install_bin)/rabs

$(install_exe): $(install_bin)/%: %
	mkdir -p $(install_bin)
	cp $< $@

install: $(install_exe) 