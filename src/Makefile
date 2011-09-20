CC = gcc


INCLUDEDIR = ../include

MEMCACHE_LIBS = -L/usr/local/lib/libmemcached-1.44/lib -lmemcached -lmemcachedutil 

GLIBS = `pkg-config --libs glib-2.0 gthread-2.0` 

REDISLIBS =  -lm -lhiredis

STATICLIBS = ../lib/libredisclient.a ../lib/libgozapxml.a

LIBS =$(MEMCACHE_LIBS) $(GLIBS) $(REDISLIBS) -lpthread   -lexpat   

CFLAGS =  -Wall -g -I. -I$(INCLUDEDIR) `pkg-config --cflags glib-2.0 gthread-2.0`   -I/usr/local/lib/libmemcached-1.44/include -DDEBUG_MODE

TARGETS = link_mining

OBJ = link_mining.o  redis_op.o log.o
all: $(TARGETS)


%.o: %.c
	$(CC) $(CFLAGS) -c  $*.c

link_mining: $(OBJ)
	$(CC) $(CFLAGS) $(LIBS) -o $@ $^ $(STATICLIBS)

install: 
	@echo "installing library(to be done)"

clean: 
	rm -f *o *~ $(TARGETS)
