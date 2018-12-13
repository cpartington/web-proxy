CC = gcc
CFLAGS = -g -Wall
LDFLAGS = -lpthread
OBJS = proxy.o csapp.o sbuf.o cbuf.o cache.o

all: proxy

csapp.o: csapp.c csapp.h
	$(CC) $(CFLAGS) -c csapp.c

proxy.o: proxy.c csapp.h
	$(CC) $(CFLAGS) -c proxy.c

cbuf.o: cbuf.c cbuf.h
	$(CC) $(CFLAGS) -c cbuf.c

sbuf.o: sbuf.c sbuf.h
	$(CC) $(CFLAGS) -c sbuf.c

cache.o: cache.c cache.h
	$(CC) $(CFLAGS) -c cache.c

proxy: $(OBJS)
	$(CC) $(CFLAGS) $(OBJS) -o proxy $(LDFLAGS)

clean:
	rm -f *~ *.o proxy core

