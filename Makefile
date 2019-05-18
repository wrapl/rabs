.PHONY: clean all install

PLATFORM = $(shell uname)

ifeq ($(PLATFORM), Mingw)
	RABS = rabs.exe
else
	RABS = rabs
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
	library.o

CFLAGS += -std=gnu11 -fstrict-aliasing -Wstrict-aliasing \
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

ifdef DEBUG
	CFLAGS += -g
	LDFLAGS += -g
else
	CFLAGS += -O2
endif

ifdef DEBUG
$(RABS): Makefile $(objects) *.h minilang/libminilang.a
	$(CC) $(objects) -o $@ $(LDFLAGS)
else
$(RABS): Makefile $(objects) *.h minilang/libminilang.a
	$(CC) $(objects) -o $@ $(LDFLAGS)
	strip $@
endif
	

clean:
	$(gmake) -C minilang clean
	rm -f $(RABS)
	rm -f *.o

PREFIX = /usr
install_bin = $(DESTDIR)$(PREFIX)/bin

install_exe = $(install_bin)/rabs

$(install_exe): $(install_bin)/%: %
	mkdir -p $(install_bin)
	cp $< $@

install: $(install_exe)
