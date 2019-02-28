.PHONY: clean all install

PLATFORM = $(shell uname)

ifeq ($(PLATFORM), Mingw)
	RABS = rabs.exe
else
	RABS = rabs
endif

all: $(RABS)

minilang/libminilang.a: minilang/Makefile
	make -C minilang PLATFORM=$(PLATFORM) libminilang.a

*.o: *.h minilang/*.h

objects = \
	cache.o \
	context.o \
	rabs.o \
	target.o \
	targetcache.o \
	targetset.o \
	util.o \
	vfs.o \
	library.o

ml_libs = \
	

CFLAGS += -std=gnu11 -fstrict-aliasing -Wstrict-aliasing \
	-I. -Iminilang -pthread -DSQLITE_THREADSAFE=0 -DGC_THREADS -D_GNU_SOURCE -D$(PLATFORM)
LDFLAGS += minilang/libminilang.a -lm -lgc -lsqlite3

ifeq ($(PLATFORM), Linux)
	LDFLAGS += -Wl,--export-dynamic -ldl
endif

ifeq ($(PLATFORM), Mingw)
	CFLAGS += -include ansicolor-w32.h
	LDFLAGS += -lregex
	objects += ansicolor-w32.o
endif

ifdef DEBUG
	CFLAGS += -g
	LDFLAGS += -g
else
	CFLAGS += -O2
endif

$(RABS): Makefile $(objects) *.h minilang/libminilang.a
	$(CC) $(objects) -o $@ $(LDFLAGS)
	strip $@

clean:
	make -C minilang clean
	rm -f rabs
	rm -f *.o

PREFIX = /usr
install_bin = $(PREFIX)/bin

install_exe = $(install_bin)/rabs

$(install_exe): $(install_bin)/%: %
	mkdir -p $(install_bin)
	cp $< $@

install: $(install_exe) 