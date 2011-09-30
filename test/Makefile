INCLUDEDIR = -I/usr/local/lib/libmemcached-1.44/include
MEMCACHE_LIBS = -L/usr/local/lib/libmemcached-1.44/lib -lmemcached -lmemcachedutil
CFLAGS = -Wall -g -I. $(INCLUDEDIR) $(MEMCACHE_LIBS)

TARGET = test 
OBJ = test.o 

all: $(TARGET)

%.o: %.c
	$(CC) $(CFLAGS) -c  $*.c

mc_set: $(OBJ)
	$(CC) $(CFLAGS) -o $@ $^

clean: 
	rm -f *o *~ $(TARGETS)