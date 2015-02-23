CC=gcc
CFLAGS=-std=c99
OBJS=socket-framework.o client-framework.o event-pump.o

all: libsockf.a test-server-mmap test-server-file test-client

%.o: %.c socket-framework.h client-framework.h event-pump.h
	$(CC) $(CFLAGS) -c -o $@ $<
libsockf.a: $(OBJS)
	ar rcs libsockf.a $(OBJS)
test-server-mmap: $(OBJS) test-server-mmap.o
	gcc -o test-server-mmap test-server-mmap.o -L. -lsockf
test-server-file: $(OBJS) test-server-file.o
	gcc -o test-server-file test-server-file.o -L. -lsockf
test-client: $(OBJS) test-client.o
	gcc -o test-client test-client.o -L. -lsockf
clean:
	rm $(OBJS) test-client test-server-mmap test-server-file libsockf.a
