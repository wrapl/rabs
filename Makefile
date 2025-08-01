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

libraries = \
	minilang/lib/libminilang.a \
	radb/libradb.a

override CFLAGS += \
	-std=gnu11 -foptimize-sibling-calls \
	-fstrict-aliasing -Wstrict-aliasing -Wall \
	-Iobj -Isrc -Iradb -Iminilang/src -Iminilang/obj -D$(PLATFORM)

obj/%_init.c: src/%.c | obj $(libraries) src/*.h 
	cc -E -P -DGENERATE_INIT $(CFLAGS) $< | sed -f sed.txt | grep -o 'INIT_CODE .*);' | sed 's/INIT_CODE //g' > $@

obj/rabs.o: obj/rabs_init.c src/*.h 
obj/context.o: obj/context_init.c src/*.h 
obj/target.o: obj/target_init.c src/*.h 
obj/target_expr.o: obj/target_expr_init.c src/*.h 
obj/target_file.o: obj/target_file_init.c src/*.h 
obj/target_scan.o: obj/target_scan_init.c src/*.h 
obj/target_meta.o: obj/target_meta_init.c src/*.h 
obj/target_symb.o: obj/target_symb_init.c src/*.h 
obj/targetset.o: obj/targetset_init.c src/*.h 
obj/library.o: obj/library_init.c src/*.h 

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

obj/%.o: src/%.c | obj $(libraries) src/*.h
	$(CC) $(CFLAGS) -c -o $@ $<

override CFLAGS += -pthread -DGC_THREADS -D_GNU_SOURCE
override LDFLAGS += minilang/lib/libminilang.a radb/libradb.a -lm -pthread -luuid

ifeq ($(MACHINE), i686)
	override CFLAGS += -fno-pic
	override LDFLAGS += -fno-pic
endif

ifeq ($(PLATFORM), Linux)
	override LDFLAGS += -Wl,--dynamic-list=src/exports.lst -ldl -lgc
	objects += obj/targetwatch.o
endif

ifeq ($(PLATFORM), Android)
	override LDFLAGS += -Wl,--dynamic-list=src/exports.lst -ldl -lgc
endif

ifeq ($(PLATFORM), FreeBSD)
	override CFLAGS += -I/usr/local/include
	override LDFLAGS += -L/usr/local/lib -lgc-threaded
endif

ifeq ($(PLATFORM), Mingw)
	override CFLAGS += -include ansicolor-w32.h
	override LDFLAGS += -lregex -lgc
	objects += obj/ansicolor-w32.o
endif

ifeq ($(PLATFORM), Darwin)
	override LDFLAGS += -ldl -lgc
endif

ifdef DEBUG
	override CFLAGS += -g
	override LDFLAGS += -g
else
	override CFLAGS += -O3 -g
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
