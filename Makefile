.PHONY: clean all install

PLATFORM = $(shell uname)
MACHINE = $(shell uname -m)

ifeq ($(PLATFORM), Mingw)
	RABS = bin/rabs.exe
else
	RABS = bin/rabs
endif

all: $(RABS)

minilang/libminilang.a: minilang/Makefile minilang/*.c minilang/*.h
	$(MAKE) -C minilang PLATFORM=$(PLATFORM) libminilang.a

*.o: *.h minilang/*.h

objects = \
	cache.o \
	context.o \
	rabs.o \
	target.o \
	targetcache.o \
	targetqueue.o \
	targetset.o \
	util.o \
	vfs.o \
	library.o \
	whereami.o

CFLAGS += -std=gnu11 -fstrict-aliasing -Wstrict-aliasing -Wall \
	-I. -Iminilang -pthread -DSQLITE_THREADSAFE=0 -DGC_THREADS -D_GNU_SOURCE -D$(PLATFORM)
LDFLAGS += minilang/libminilang.a -lm

ifeq ($(PLATFORM), Linux)
	LDFLAGS += -Wl,--export-dynamic -ldl -lgc -lsqlite3
	objects += targetwatch.o
endif

ifeq ($(PLATFORM), FreeBSD)
	CFLAGS += -I/usr/local/include
	LDFLAGS += -L/usr/local/lib -lgc-threaded -lsqlite3
endif

ifeq ($(PLATFORM), Mingw)
	CFLAGS += -include ansicolor-w32.h
	LDFLAGS += -lregex -lgc -lsqlite3
	objects += ansicolor-w32.o
endif

ifeq ($(PLATFORM), Darwin)
	LDFLAGS += -ldl -lgc -lsqlite3
endif

ifdef DEBUG
	CFLAGS += -g
	LDFLAGS += -g
else
	CFLAGS += -O3 -g
endif

ifdef DEBUG
$(RABS): Makefile $(objects) *.h minilang/libminilang.a
	$(CC) $(objects) -o $@ $(LDFLAGS)
else
$(RABS): Makefile $(objects) *.h minilang/libminilang.a
	mkdir -p bin
	$(CC) $(objects) -o $@ $(LDFLAGS)
	strip $@
endif
	

clean:
	$(MAKE) -C minilang clean
	rm -f $(RABS)
	rm -f *.o

PREFIX = /usr
install_bin = $(DESTDIR)$(PREFIX)/bin

install_exe = $(DESTDIR)$(PREFIX)/bin/rabs

$(install_exe): $(DESTDIR)$(PREFIX)/%: %
	mkdir -p $(DESTDIR)$(PREFIX)/bin
	cp $< $@

install: $(install_exe)
