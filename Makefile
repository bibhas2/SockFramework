CC=gcc
CFLAGS=-std=c99
OBJS=socket-framework.o 

all: test-server-mmap test-server-file

%.o: %.c socket-framework.h
	$(CC) $(CFLAGS) -c -o $@ $<
test-server-mmap: $(OBJS) test-server-mmap.o
	gcc -o test-server-mmap $(OBJS) test-server-mmap.o
test-server-file: $(OBJS) test-server-file.o
	gcc -o test-server-file $(OBJS) test-server-file.o

