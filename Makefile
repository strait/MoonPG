CC=gcc

objs := luapg.o session.o common.o geotypes.o

all: CFLAGS=-pedantic -Wall -O2 -std=c99 -shared -fpic -I /usr/include/lua5.1
all: luapg

debug: CFLAGS=-gdwarf-2 -g3 -pedantic -Wall -O0 -std=c99 -shared -fpic -I /usr/include/lua5.1
debug: luapg

luapg: $(objs)
	$(CC) $(CFLAGS) $(objs) -o luapg.so -lpq -lpthread

%.o: %.c %.h common.h
	$(CC) -c $(CFLAGS) $< -o $@

clean:
	rm *.o
