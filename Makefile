CC=gcc
CFLAGS=-gdwarf-2 -g3 -pedantic -Wall -O0 -std=c99 -shared -fpic -I /usr/include/lua5.1

objs := luapg.o session.o result.o common.o geotypes.o

all: luapg.so

luapg.so: $(objs)
	$(CC) $(CFLAGS) $(objs) -o luapg.so -lpq -lpthread

%.o: %.c %.h common.h
	$(CC) -c $(CFLAGS) $< -o $@

session.o: result.h

clean:
	rm *.o
