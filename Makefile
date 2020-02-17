.PHONY: clean all install

PLATFORM = $(shell uname)
MACHINE = $(shell uname -m)

ifeq ($(PLATFORM), Mingw)
	RABS = bin/rabs.exe
else
	RABS = bin/rabs
endif

all: $(RABS)

SUBDIRS = obj bin lib

$(SUBDIRS):
	mkdir -p $@

minilang/lib/libminilang.a: minilang/Makefile minilang/src/*.c minilang/src/*.h
	$(MAKE) -C minilang PLATFORM=$(PLATFORM) lib/libminilang.a

radb/libradb.a: radb/Makefile radb/*.c radb/*.h
	$(MAKE) -C radb PLATFORM=$(PLATFORM) libradb.a

*.o: *.h minilang/src/*.h

obj/%.o: src/%.c | obj
	$(CC) $(CFLAGS) -c -o $@ $< 

objects = \
	obj/cache.o \
	obj/context.o \
	obj/rabs.o \
	obj/target.o \
	obj/target_expr.o \
	obj/target_file.o \
	obj/target_meta.o \
	obj/target_scan.o \
	obj/target_symb.o \
	obj/targetcache.o \
	obj/targetqueue.o \
	obj/targetset.o \
	obj/util.o \
	obj/vfs.o \
	obj/library.o \
	obj/whereami.o

CFLAGS += -std=gnu11 -fstrict-aliasing -Wstrict-aliasing -Wall \
	-Iobj -Isrc -Iradb -Iminilang/src -Iradb -pthread -DSQLITE_THREADSAFE=0 -DGC_THREADS -D_GNU_SOURCE -D$(PLATFORM)
LDFLAGS += minilang/lib/libminilang.a radb/libradb.a -lm -pthread

ifeq ($(MACHINE), i686)
	CFLAGS += -fno-pic
	LDFLAGS += -fno-pic
endif

ifeq ($(PLATFORM), Linux)
	LDFLAGS += -Wl,--dynamic-list=src/exports.lst -ldl -lgc
	objects += obj/targetwatch.o
endif

ifeq ($(PLATFORM), FreeBSD)
	CFLAGS += -I/usr/local/include
	LDFLAGS += -L/usr/local/lib -lgc-threaded
endif

ifeq ($(PLATFORM), Mingw)
	CFLAGS += -include ansicolor-w32.h
	LDFLAGS += -lregex -lgc
	objects += obj/ansicolor-w32.o
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
$(RABS): Makefile $(objects) src/*.h minilang/lib/libminilang.a radb/libradb.a src/exports.lst bin
	$(CC) $(objects) -o $@ $(LDFLAGS)
else
$(RABS): Makefile $(objects) src/*.h minilang/lib/libminilang.a radb/libradb.a src/exports.lst bin
	$(CC) $(objects) -o $@ $(LDFLAGS)
	#strip $@
endif
	

clean:
	$(MAKE) -C minilang clean
	$(MAKE) -C radb clean
	rm -rf bin obj

PREFIX = /usr
install_bin = $(DESTDIR)$(PREFIX)/bin

install_exe = $(DESTDIR)$(PREFIX)/bin/rabs

$(install_exe): $(DESTDIR)$(PREFIX)/%: %
	mkdir -p $(DESTDIR)$(PREFIX)/bin
	cp $< $@

install: $(install_exe)
