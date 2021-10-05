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
	$(MAKE) -C radb PLATFORM=$(PLATFORM) libradb.a RADB_MEM=GC

obj/%_init.c: src/%.c | obj src/*.h 
	echo "" > $@
	cc -E -P -DGENERATE_INIT $(CFLAGS) $< | sed -f sed.txt | grep -o 'INIT_CODE .*);' | sed 's/INIT_CODE //g' > $@

obj/rabs.o: obj/rabs_init.c
obj/context.o: obj/context_init.c
obj/target.o: obj/target_init.c
obj/target_expr.o: obj/target_expr_init.c
obj/target_file.o: obj/target_file_init.c
obj/target_scan.o: obj/target_scan_init.c
obj/target_meta.o: obj/target_meta_init.c
obj/target_symb.o: obj/target_symb_init.c
obj/targetset.o: obj/targetset_init.c
obj/library.o: obj/library_init.c

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

libraries = \
	minilang/lib/libminilang.a radb/libradb.a

obj/%.o: src/%.c | obj $(libraries) src/*.h
	$(CC) $(CFLAGS) -c -o $@ $<

CFLAGS += \
	-std=gnu11 -foptimize-sibling-calls \
	-fstrict-aliasing -Wstrict-aliasing -Wall \
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
$(RABS): Makefile $(objects) $(libraries) src/*.h src/exports.lst bin
	$(CC) $(objects) $(libraries) -o $@ $(LDFLAGS)
else
$(RABS): Makefile $(objects) $(libraries) src/*.h src/exports.lst bin
	$(CC) $(objects) $(libraries) -o $@ $(LDFLAGS)
	# strip $@
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
