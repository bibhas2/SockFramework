CC=gcc
CFLAGS=-std=c99
OBJS=socket-framework.o test-server.o

%.o: %.c socket-framework.h
	$(CC) $(CFLAGS) -c -o $@ $<

test-server: $(OBJS)
	gcc -o test-server $(OBJS)
