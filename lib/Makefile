.PHONY: clean all install

all: rabs/csv.so

*.o: ../*.h ../minilang/*.h

CFLAGS += -std=gnu11 -fstrict-aliasing -Wstrict-aliasing \
	-I. -I.. -I../minilang -pthread -DSQLITE_THREADSAFE=0 -DGC_THREADS -D_GNU_SOURCE -fpic
LDFLAGS += -export-dynamic -lm -ldl -lgc -lsqlite3

ifdef DEBUG
	CFLAGS += -g
	LDFLAGS += -g
else
	CFLAGS += -O2
endif

rabs/csv.so: Makefile csv.o libcsv.o *.h
	mkdir -p rabs
	gcc -shared csv.o libcsv.o $(LDFLAGS) -o $@
	strip $@

clean:
	rm -f *.so
	rm -f *.o

PREFIX = /usr
