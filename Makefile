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

radb/libradb.a: radb/Makefile radb/*.c radb/*.h
	$(MAKE) -C radb PLATFORM=$(PLATFORM) libradb.a

*.o: *.h minilang/*.h

objects = \
	cache_radb.o \
	context.o \
	rabs.o \
	target.o \
	target_expr.o \
	target_file.o \
	target_meta.o \
	target_scan.o \
	target_symb.o \
	targetcache.o \
	targetqueue.o \
	targetset.o \
	util.o \
	vfs.o \
	library.o \
	whereami.o

CFLAGS += -std=gnu11 -fstrict-aliasing -Wstrict-aliasing -Wall \
	-I. -Iminilang -Iradb -pthread -DSQLITE_THREADSAFE=0 -DGC_THREADS -D_GNU_SOURCE -D$(PLATFORM)
LDFLAGS += minilang/libminilang.a radb/libradb.a -lm

ifeq ($(MACHINE), i686)
	CFLAGS += "-fno-pic"
endif

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
$(RABS): Makefile $(objects) *.h minilang/libminilang.a radb/libradb.a
	$(CC) $(objects) -o $@ $(LDFLAGS)
else
$(RABS): Makefile $(objects) *.h minilang/libminilang.a radb/libradb.a
	mkdir -p bin
	$(CC) $(objects) -o $@ $(LDFLAGS)
	#strip $@
endif
	

clean:
	$(MAKE) -C minilang clean
	$(MAKE) -C radb clean
	rm -f $(RABS)
	rm -f *.o

PREFIX = /usr
install_bin = $(DESTDIR)$(PREFIX)/bin

install_exe = $(DESTDIR)$(PREFIX)/bin/rabs

$(install_exe): $(DESTDIR)$(PREFIX)/%: %
	mkdir -p $(DESTDIR)$(PREFIX)/bin
	cp $< $@

install: $(install_exe)
