CC=gcc
CFLAGS=-std=c99
OBJS=socket-framework.o client-framework.o

all: test-server-mmap test-server-file test-client

%.o: %.c socket-framework.h client-framework.h
	$(CC) $(CFLAGS) -c -o $@ $<
test-server-mmap: $(OBJS) test-server-mmap.o
	gcc -o test-server-mmap $(OBJS) test-server-mmap.o
test-server-file: $(OBJS) test-server-file.o
	gcc -o test-server-file $(OBJS) test-server-file.o
test-client: $(OBJS) test-client.o
	gcc -o test-client $(OBJS) test-client.o

