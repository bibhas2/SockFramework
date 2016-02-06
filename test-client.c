#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <netdb.h>
#include <errno.h>
#include <fcntl.h>

#include "event-pump.h"

#define DIE(value, message) if (value < 0) {perror(message); abort();}

#define _info printf("INFO: "); printf

char req[1024];

static void onReadable(SocketRec *rec) {
	char buff[256];

	int len = read(rec->socket, buff, sizeof(buff));

	if (len == 0) {
		//Orderly disconnect
		pumpStop(rec->pump);

		return;
	}
	printf("%.*s", len, buff);
}

static void onWritable(SocketRec *rec) {
	//Write the request
	puts(req);
	int status = write(rec->socket, req, strlen(req));

	assert(status > 0);

	rec->onWritable = NULL;
}

static void onConnect(SocketRec *rec, int status) {
	printf("Conn status: %d\n", status);
	assert(status == 1);

	rec->onWritable = onWritable;
}

static void onTimeout(SocketRec *rec) {
	close(rec->socket);
	pumpStop(rec->pump);
}

int main(int argc, char **argv) {
	if (argc < 4) {
		puts("Usage: test_client host port path");
		return 1;
	}

	const char *host = argv[1];
	const char *port = argv[2];
	const char *path = argv[3];

  char *fmt = "GET %s HTTP/1.1\r\n"
          "Host: %s:%s\r\n"
          "Accept: */*\r\n"
          "\r\n";
	snprintf(req, sizeof(req), fmt, path, host, port);

	EventPump *pump = newEventPump();
	SocketRec *rec = pumpRegisterClient(pump,
		host, port, req);
	rec->onConnect = onConnect;
	rec->onReadable = onReadable;
	rec->onWritable = NULL;
	rec->onTimeout = onTimeout;

	pumpStart(pump);

	deleteEventPump(pump);

	return 0;
}
